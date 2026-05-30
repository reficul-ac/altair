#include "plan_visualization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace animus::app
{
namespace
{

constexpr double earth_radius_m = 6371008.8;
constexpr double deg_to_rad = 3.14159265358979323846 / 180.0;

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue
{
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;
    Storage value;
};

class JsonParser
{
  public:
    explicit JsonParser(std::string text) : text_(std::move(text))
    {
    }

    JsonValue parse()
    {
        JsonValue value = parse_value();
        skip_ws();
        if (pos_ != text_.size())
        {
            fail("unexpected trailing input");
        }
        return value;
    }

  private:
    [[noreturn]] void fail(const std::string &message) const
    {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(pos_) + ": " +
                                 message);
    }

    void skip_ws()
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r'))
        {
            ++pos_;
        }
    }

    bool consume(const char c)
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(const char c)
    {
        if (!consume(c))
        {
            fail(std::string("expected '") + c + "'");
        }
    }

    JsonValue parse_value()
    {
        skip_ws();
        if (pos_ >= text_.size())
        {
            fail("unexpected end of input");
        }
        const char c = text_[pos_];
        if (c == '{')
        {
            return JsonValue{parse_object()};
        }
        if (c == '[')
        {
            return JsonValue{parse_array()};
        }
        if (c == '"')
        {
            return JsonValue{parse_string()};
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            return JsonValue{parse_number()};
        }
        if (text_.compare(pos_, 4U, "true") == 0)
        {
            pos_ += 4U;
            return JsonValue{true};
        }
        if (text_.compare(pos_, 5U, "false") == 0)
        {
            pos_ += 5U;
            return JsonValue{false};
        }
        if (text_.compare(pos_, 4U, "null") == 0)
        {
            pos_ += 4U;
            return JsonValue{nullptr};
        }
        fail("unexpected value");
    }

    JsonObject parse_object()
    {
        expect('{');
        JsonObject object;
        if (consume('}'))
        {
            return object;
        }
        while (true)
        {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"')
            {
                fail("expected object key");
            }
            std::string key = parse_string();
            expect(':');
            object.emplace(std::move(key), parse_value());
            if (consume('}'))
            {
                break;
            }
            expect(',');
        }
        return object;
    }

    JsonArray parse_array()
    {
        expect('[');
        JsonArray array;
        if (consume(']'))
        {
            return array;
        }
        while (true)
        {
            array.push_back(parse_value());
            if (consume(']'))
            {
                break;
            }
            expect(',');
        }
        return array;
    }

    std::string parse_string()
    {
        expect('"');
        std::string result;
        while (pos_ < text_.size())
        {
            const char c = text_[pos_++];
            if (c == '"')
            {
                return result;
            }
            if (c != '\\')
            {
                result.push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
            {
                fail("unterminated escape");
            }
            const char esc = text_[pos_++];
            switch (esc)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(esc);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                if (pos_ + 4U > text_.size())
                {
                    fail("short unicode escape");
                }
                result.push_back('?');
                pos_ += 4U;
                break;
            default:
                fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    double parse_number()
    {
        const std::size_t begin = pos_;
        if (text_[pos_] == '-')
        {
            ++pos_;
        }
        if (pos_ >= text_.size())
        {
            fail("invalid number");
        }
        if (text_[pos_] == '0')
        {
            ++pos_;
        }
        else if (text_[pos_] >= '1' && text_[pos_] <= '9')
        {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
            {
                ++pos_;
            }
        }
        else
        {
            fail("invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
            {
                fail("invalid fraction");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
            {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
            {
                ++pos_;
            }
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9')
            {
                fail("invalid exponent");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
            {
                ++pos_;
            }
        }
        const double parsed = std::stod(text_.substr(begin, pos_ - begin));
        if (!std::isfinite(parsed))
        {
            fail("non-finite number");
        }
        return parsed;
    }

    std::string text_;
    std::size_t pos_ = 0U;
};

const JsonObject *as_object(const JsonValue &value)
{
    return std::get_if<JsonObject>(&value.value);
}

const JsonArray *as_array(const JsonValue &value)
{
    return std::get_if<JsonArray>(&value.value);
}

const std::string *as_string(const JsonValue &value)
{
    return std::get_if<std::string>(&value.value);
}

const double *as_number(const JsonValue &value)
{
    return std::get_if<double>(&value.value);
}

const JsonValue *field(const JsonObject &object, const std::string &key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const JsonObject *object_field(const JsonObject &object, const std::string &key)
{
    const JsonValue *value = field(object, key);
    return value == nullptr ? nullptr : as_object(*value);
}

const JsonArray *array_field(const JsonObject &object, const std::string &key)
{
    const JsonValue *value = field(object, key);
    return value == nullptr ? nullptr : as_array(*value);
}

const JsonArray *array_field_recursive(const JsonValue &value, const std::string &key)
{
    if (const JsonObject *object = as_object(value))
    {
        if (const JsonArray *direct = array_field(*object, key))
        {
            return direct;
        }
        for (const auto &[child_key, child_value] : *object)
        {
            (void)child_key;
            if (const JsonArray *nested = array_field_recursive(child_value, key))
            {
                return nested;
            }
        }
    }
    else if (const JsonArray *array = as_array(value))
    {
        for (const JsonValue &child : *array)
        {
            if (const JsonArray *nested = array_field_recursive(child, key))
            {
                return nested;
            }
        }
    }
    return nullptr;
}

std::optional<std::string> string_field(const JsonObject &object, const std::string &key)
{
    const JsonValue *value = field(object, key);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    const std::string *text = as_string(*value);
    return text == nullptr ? std::nullopt : std::optional<std::string>(*text);
}

std::optional<double> number_field(const JsonObject &object, const std::string &key)
{
    const JsonValue *value = field(object, key);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    const double *number = as_number(*value);
    return number == nullptr ? std::nullopt : std::optional<double>(*number);
}

bool valid_lat_lon(const double lat_deg, const double lon_deg)
{
    return std::isfinite(lat_deg) && std::isfinite(lon_deg) && lat_deg >= -90.0 &&
           lat_deg <= 90.0 && lon_deg >= -180.0 && lon_deg <= 180.0;
}

PlanGeoPoint point_from_lat_lon_alt(const double lat_deg,
                                    const double lon_deg,
                                    const std::optional<double> alt_m,
                                    const std::string &context)
{
    if (!valid_lat_lon(lat_deg, lon_deg))
    {
        throw std::runtime_error(context + " has invalid latitude/longitude");
    }
    if (alt_m && !std::isfinite(*alt_m))
    {
        throw std::runtime_error(context + " has invalid altitude");
    }
    return {lat_deg, lon_deg, alt_m};
}

PlanGeoPoint point_from_array(const JsonValue &value,
                              const std::size_t lat_index,
                              const std::size_t lon_index,
                              const std::optional<std::size_t> alt_index,
                              const std::string &context)
{
    const JsonArray *array = as_array(value);
    if (array == nullptr || array->size() <= std::max(lat_index, lon_index))
    {
        throw std::runtime_error(context + " must be a coordinate array");
    }
    const double *lat = as_number((*array)[lat_index]);
    const double *lon = as_number((*array)[lon_index]);
    if (lat == nullptr || lon == nullptr)
    {
        throw std::runtime_error(context + " latitude/longitude must be numeric");
    }
    std::optional<double> alt;
    if (alt_index && array->size() > *alt_index)
    {
        const double *alt_value = as_number((*array)[*alt_index]);
        if (alt_value == nullptr)
        {
            throw std::runtime_error(context + " altitude must be numeric");
        }
        alt = *alt_value;
    }
    return point_from_lat_lon_alt(*lat, *lon, alt, context);
}

PlanGeoPoint point_from_coordinate_array(const JsonArray &array, const std::string &context)
{
    if (array.size() < 2U)
    {
        throw std::runtime_error(context + " must be a coordinate array");
    }
    const double *lat = as_number(array[0U]);
    const double *lon = as_number(array[1U]);
    if (lat == nullptr || lon == nullptr)
    {
        throw std::runtime_error(context + " latitude/longitude must be numeric");
    }
    return point_from_lat_lon_alt(*lat, *lon, std::nullopt, context);
}

std::vector<PlanGeoPoint> points_from_array(const JsonArray &array, const std::string &context)
{
    std::vector<PlanGeoPoint> points;
    points.reserve(array.size());
    for (std::size_t index = 0; index < array.size(); ++index)
    {
        points.push_back(point_from_array(array[index], 0U, 1U, 2U, context));
    }
    return points;
}

void add_complex_visual_points(const JsonObject &item,
                               const std::string &label,
                               PlanVisualizationData &data,
                               PlanVisualizationLoadResult &result)
{
    const std::array<const char *, 3> fields = {"VisualTransectPoints", "polygon", "polyline"};
    const JsonValue item_value{item};
    for (const char *name : fields)
    {
        const JsonArray *points = array_field_recursive(item_value, name);
        if (points == nullptr)
        {
            continue;
        }
        PlanPolyline outline;
        outline.label = label;
        outline.points = points_from_array(*points, std::string("complex item ") + name);
        if (outline.points.size() >= 2U)
        {
            data.complex_outlines.push_back(std::move(outline));
            return;
        }
    }
    ++data.unsupported_item_count;
    result.diagnostics.push_back(label + " has no supported visual coordinate array");
}

void parse_mission_items(const JsonObject &mission,
                         PlanVisualizationData &data,
                         PlanVisualizationLoadResult &result)
{
    const JsonArray *items = array_field(mission, "items");
    if (items == nullptr)
    {
        throw std::runtime_error("plan mission.items must be an array");
    }
    for (std::size_t index = 0; index < items->size(); ++index)
    {
        const JsonObject *item = as_object((*items)[index]);
        if (item == nullptr)
        {
            throw std::runtime_error("mission item " + std::to_string(index) + " must be object");
        }
        const std::string type = string_field(*item, "type").value_or("SimpleItem");
        if (type != "SimpleItem")
        {
            add_complex_visual_points(
                *item, "complex item " + std::to_string(index + 1U), data, result);
            continue;
        }
        const JsonArray *params = array_field(*item, "params");
        if (params == nullptr || params->size() < 6U)
        {
            throw std::runtime_error("SimpleItem " + std::to_string(index + 1U) +
                                     " params must include lat/lon slots");
        }
        const double *lat = as_number((*params)[4U]);
        const double *lon = as_number((*params)[5U]);
        if (lat == nullptr || lon == nullptr)
        {
            throw std::runtime_error("SimpleItem " + std::to_string(index + 1U) +
                                     " lat/lon params must be numeric");
        }
        std::optional<double> alt;
        if (params->size() > 6U)
        {
            const double *alt_value = as_number((*params)[6U]);
            if (alt_value != nullptr)
            {
                alt = *alt_value;
            }
        }
        PlanWaypoint waypoint;
        waypoint.point =
            point_from_lat_lon_alt(*lat, *lon, alt, "SimpleItem " + std::to_string(index + 1U));
        waypoint.label = std::to_string(data.mission_waypoints.size() + 1U);
        data.mission_waypoints.push_back(std::move(waypoint));
    }
}

void parse_geofence(const JsonObject &root, PlanVisualizationData &data)
{
    const JsonObject *geo_fence = object_field(root, "geoFence");
    if (geo_fence == nullptr)
    {
        return;
    }
    if (const JsonArray *polygons = array_field(*geo_fence, "polygons"))
    {
        for (std::size_t index = 0; index < polygons->size(); ++index)
        {
            const JsonObject *entry = as_object((*polygons)[index]);
            const JsonArray *polygon =
                entry == nullptr ? as_array((*polygons)[index]) : array_field(*entry, "polygon");
            if (polygon == nullptr)
            {
                throw std::runtime_error("geoFence polygon must contain polygon coordinates");
            }
            PlanPolyline parsed;
            parsed.label = "geofence";
            parsed.points = points_from_array(*polygon, "geoFence polygon");
            if (parsed.points.size() < 3U)
            {
                throw std::runtime_error("geoFence polygon needs at least three points");
            }
            data.geofence_polygons.push_back(std::move(parsed));
        }
    }
    if (const JsonArray *circles = array_field(*geo_fence, "circles"))
    {
        for (std::size_t index = 0; index < circles->size(); ++index)
        {
            const JsonObject *entry = as_object((*circles)[index]);
            if (entry == nullptr)
            {
                throw std::runtime_error("geoFence circle must be an object");
            }
            const JsonObject *circle_object = object_field(*entry, "circle");
            const JsonObject &circle = circle_object == nullptr ? *entry : *circle_object;
            const JsonArray *center = array_field(circle, "center");
            const auto radius = number_field(circle, "radius");
            if (center == nullptr || !radius || !std::isfinite(*radius) || *radius <= 0.0)
            {
                throw std::runtime_error("geoFence circle must have center and positive radius");
            }
            data.geofence_circles.push_back(
                {point_from_coordinate_array(*center, "geoFence circle center"), *radius});
        }
    }
}

void parse_rally_points(const JsonObject &root, PlanVisualizationData &data)
{
    const JsonObject *rally = object_field(root, "rallyPoints");
    if (rally == nullptr)
    {
        return;
    }
    const JsonArray *points = array_field(*rally, "points");
    if (points == nullptr)
    {
        return;
    }
    for (std::size_t index = 0; index < points->size(); ++index)
    {
        PlanWaypoint waypoint;
        const JsonObject *object = as_object((*points)[index]);
        if (object != nullptr)
        {
            const auto lat = number_field(*object, "latitude");
            const auto lon = number_field(*object, "longitude");
            const auto alt = number_field(*object, "altitude");
            if (!lat || !lon)
            {
                throw std::runtime_error("rally point object needs latitude and longitude");
            }
            waypoint.point = point_from_lat_lon_alt(*lat, *lon, alt, "rally point");
        }
        else
        {
            waypoint.point = point_from_array((*points)[index], 0U, 1U, 2U, "rally point");
        }
        waypoint.label = "R" + std::to_string(index + 1U);
        data.rally_points.push_back(std::move(waypoint));
    }
}

PlanVisualizationData parse_plan(const JsonValue &json, PlanVisualizationLoadResult &result)
{
    const JsonObject *root = as_object(json);
    if (root == nullptr)
    {
        throw std::runtime_error("plan file root must be an object");
    }
    const auto file_type = string_field(*root, "fileType");
    if (!file_type || *file_type != "Plan")
    {
        throw std::runtime_error("plan fileType must be Plan");
    }
    const JsonObject *mission = object_field(*root, "mission");
    if (mission == nullptr)
    {
        throw std::runtime_error("plan mission must be an object");
    }
    PlanVisualizationData data;
    parse_mission_items(*mission, data, result);
    parse_geofence(*root, data);
    parse_rally_points(*root, data);
    data.route_distance_m = plan_route_distance_m(data.mission_waypoints);
    return data;
}

double track_distance_m(const animus::telemetry_core::Track &track)
{
    double total = 0.0;
    std::optional<animus::telemetry_core::TelemetrySample> previous;
    for (const auto &sample : track.samples)
    {
        if (!sample.fields.position)
        {
            continue;
        }
        if (previous)
        {
            total += geo_distance_m(
                previous->lat_deg, previous->lon_deg, sample.lat_deg, sample.lon_deg);
        }
        previous = sample;
    }
    return total;
}

std::optional<double> sample_altitude_m(const animus::telemetry_core::TelemetrySample &sample)
{
    if (sample.altitude_relative_m)
    {
        return sample.altitude_relative_m;
    }
    return sample.altitude_msl_m;
}

std::optional<double> nearest_track_distance_m(const PlanGeoPoint &point,
                                               const animus::telemetry_core::Track &track)
{
    std::optional<double> nearest;
    for (const auto &sample : track.samples)
    {
        if (!sample.fields.position)
        {
            continue;
        }
        const double distance =
            geo_distance_m(point.lat_deg, point.lon_deg, sample.lat_deg, sample.lon_deg);
        nearest = nearest ? std::min(*nearest, distance) : distance;
    }
    return nearest;
}

struct LocalPoint
{
    double x_m = 0.0;
    double y_m = 0.0;
};

LocalPoint to_local_point(const PlanGeoPoint &origin, const double lat_deg, const double lon_deg)
{
    const double mean_lat = (origin.lat_deg + lat_deg) * 0.5 * deg_to_rad;
    return {earth_radius_m * (lon_deg - origin.lon_deg) * deg_to_rad * std::cos(mean_lat),
            earth_radius_m * (lat_deg - origin.lat_deg) * deg_to_rad};
}

double local_distance_m(const LocalPoint a, const LocalPoint b)
{
    const double dx = b.x_m - a.x_m;
    const double dy = b.y_m - a.y_m;
    return std::hypot(dx, dy);
}

PlanGeoPoint
interpolate_point(const PlanGeoPoint &from, const PlanGeoPoint &to, const double fraction)
{
    PlanGeoPoint point;
    point.lat_deg = from.lat_deg + (to.lat_deg - from.lat_deg) * fraction;
    point.lon_deg = from.lon_deg + (to.lon_deg - from.lon_deg) * fraction;
    if (from.alt_m && to.alt_m)
    {
        point.alt_m = *from.alt_m + (*to.alt_m - *from.alt_m) * fraction;
    }
    else if (fraction <= 0.0 && from.alt_m)
    {
        point.alt_m = from.alt_m;
    }
    else if (fraction >= 1.0 && to.alt_m)
    {
        point.alt_m = to.alt_m;
    }
    return point;
}

struct RouteSegmentCandidate
{
    std::size_t segment_index = 0U;
    double clamped_fraction = 0.0;
    double unclamped_fraction = 0.0;
    double cross_track_error_m = 0.0;
    double along_route_m = 0.0;
    PlanGeoPoint nearest_point;
};

std::vector<double> cumulative_route_distances(const std::vector<PlanWaypoint> &waypoints)
{
    std::vector<double> distances(waypoints.size(), 0.0);
    for (std::size_t index = 1U; index < waypoints.size(); ++index)
    {
        distances[index] =
            distances[index - 1U] + geo_distance_m(waypoints[index - 1U].point.lat_deg,
                                                   waypoints[index - 1U].point.lon_deg,
                                                   waypoints[index].point.lat_deg,
                                                   waypoints[index].point.lon_deg);
    }
    return distances;
}

std::optional<RouteSegmentCandidate>
nearest_route_segment(const std::vector<PlanWaypoint> &waypoints,
                      const std::vector<double> &cumulative_distances,
                      const animus::telemetry_core::TelemetrySample &sample)
{
    if (!sample.fields.position || waypoints.size() < 2U)
    {
        return std::nullopt;
    }

    std::optional<RouteSegmentCandidate> nearest;
    const PlanGeoPoint &origin = waypoints.front().point;
    const LocalPoint sample_point = to_local_point(origin, sample.lat_deg, sample.lon_deg);
    for (std::size_t index = 1U; index < waypoints.size(); ++index)
    {
        const PlanGeoPoint &from = waypoints[index - 1U].point;
        const PlanGeoPoint &to = waypoints[index].point;
        const LocalPoint a = to_local_point(origin, from.lat_deg, from.lon_deg);
        const LocalPoint b = to_local_point(origin, to.lat_deg, to.lon_deg);
        const double dx = b.x_m - a.x_m;
        const double dy = b.y_m - a.y_m;
        const double length_sq = dx * dx + dy * dy;
        if (length_sq <= 0.0)
        {
            continue;
        }
        const double raw_fraction =
            ((sample_point.x_m - a.x_m) * dx + (sample_point.y_m - a.y_m) * dy) / length_sq;
        const double fraction = std::clamp(raw_fraction, 0.0, 1.0);
        const LocalPoint projected{a.x_m + dx * fraction, a.y_m + dy * fraction};
        const double error_m = local_distance_m(sample_point, projected);
        const double segment_m = cumulative_distances[index] - cumulative_distances[index - 1U];
        RouteSegmentCandidate candidate;
        candidate.segment_index = index - 1U;
        candidate.clamped_fraction = fraction;
        candidate.unclamped_fraction = raw_fraction;
        candidate.cross_track_error_m = error_m;
        candidate.along_route_m = cumulative_distances[index - 1U] + segment_m * fraction;
        candidate.nearest_point = interpolate_point(from, to, fraction);
        if (!nearest || candidate.cross_track_error_m < nearest->cross_track_error_m)
        {
            nearest = candidate;
        }
    }
    return nearest;
}

PlanActualDeviation make_deviation(const std::vector<PlanWaypoint> &waypoints,
                                   const std::vector<double> &cumulative_distances,
                                   const double route_distance_m,
                                   const animus::telemetry_core::TelemetrySample &sample)
{
    const auto nearest = nearest_route_segment(waypoints, cumulative_distances, sample);
    PlanActualDeviation deviation;
    deviation.time_s = sample.time_s;
    if (!nearest)
    {
        return deviation;
    }
    deviation.nearest_route_point = nearest->nearest_point;
    deviation.cross_track_error_m = nearest->cross_track_error_m;
    deviation.progress.segment_index = nearest->segment_index;
    deviation.progress.from_waypoint_index = nearest->segment_index;
    deviation.progress.to_waypoint_index = nearest->segment_index + 1U;
    deviation.progress.segment_fraction = nearest->clamped_fraction;
    deviation.progress.along_route_m = nearest->along_route_m;
    deviation.progress.route_completion_ratio =
        route_distance_m > 0.0 ? nearest->along_route_m / route_distance_m : 0.0;
    deviation.progress.next_waypoint_index = nearest->segment_index + 1U;
    if (deviation.nearest_route_point.alt_m)
    {
        if (const auto altitude = sample_altitude_m(sample))
        {
            deviation.altitude_error_m = *altitude - *deviation.nearest_route_point.alt_m;
        }
    }
    return deviation;
}

std::optional<animus::telemetry_core::TelemetrySample>
track_sample_at(const animus::telemetry_core::Track &track, const double current_time_s)
{
    std::optional<animus::telemetry_core::TelemetrySample> selected;
    double selected_delta_s = 0.0;
    for (const auto &sample : track.samples)
    {
        const double delta_s = std::abs(sample.time_s - current_time_s);
        if (!selected || delta_s < selected_delta_s)
        {
            selected = sample;
            selected_delta_s = delta_s;
        }
        if (sample.time_s > current_time_s && delta_s > selected_delta_s)
        {
            break;
        }
    }
    return selected;
}

} // namespace

double geo_distance_m(const double a_lat_deg,
                      const double a_lon_deg,
                      const double b_lat_deg,
                      const double b_lon_deg)
{
    const double lat1 = a_lat_deg * deg_to_rad;
    const double lat2 = b_lat_deg * deg_to_rad;
    const double dlat = (b_lat_deg - a_lat_deg) * deg_to_rad;
    const double dlon = (b_lon_deg - a_lon_deg) * deg_to_rad;
    const double sin_dlat = std::sin(dlat * 0.5);
    const double sin_dlon = std::sin(dlon * 0.5);
    const double h = sin_dlat * sin_dlat + std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
    return earth_radius_m * 2.0 * std::atan2(std::sqrt(h), std::sqrt(std::max(0.0, 1.0 - h)));
}

double plan_route_distance_m(const std::vector<PlanWaypoint> &waypoints)
{
    double total = 0.0;
    for (std::size_t index = 1U; index < waypoints.size(); ++index)
    {
        total += geo_distance_m(waypoints[index - 1U].point.lat_deg,
                                waypoints[index - 1U].point.lon_deg,
                                waypoints[index].point.lat_deg,
                                waypoints[index].point.lon_deg);
    }
    return total;
}

PlanVisualizationLoadResult load_plan_visualization(const std::filesystem::path &path)
{
    PlanVisualizationLoadResult result;
    std::ifstream input(path);
    if (!input)
    {
        result.error = "unable to read plan file: " + path.string();
        return result;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    try
    {
        JsonParser parser(buffer.str());
        result.data = parse_plan(parser.parse(), result);
    }
    catch (const std::exception &error)
    {
        result.error = error.what();
    }
    return result;
}

PlanTrackComparison compare_plan_to_track(const PlanVisualizationData &plan,
                                          const animus::telemetry_core::Track &track)
{
    PlanTrackComparison comparison;
    comparison.planned_route_m = plan.route_distance_m;
    comparison.selected_track_m = track_distance_m(track);
    if (!plan.mission_waypoints.empty())
    {
        comparison.first_waypoint_nearest_track_m =
            nearest_track_distance_m(plan.mission_waypoints.front().point, track);
        comparison.last_waypoint_nearest_track_m =
            nearest_track_distance_m(plan.mission_waypoints.back().point, track);
    }
    return comparison;
}

PlanActualAggregate compare_plan_actual(const PlanVisualizationData &plan,
                                        const animus::telemetry_core::Track &track,
                                        const double current_time_s)
{
    PlanActualAggregate aggregate;
    aggregate.planned_route_m = plan.route_distance_m;
    aggregate.selected_track_m = track_distance_m(track);
    if (plan.mission_waypoints.size() < 2U || plan.route_distance_m <= 0.0)
    {
        return aggregate;
    }

    const std::vector<double> cumulative_distances =
        cumulative_route_distances(plan.mission_waypoints);
    double cross_track_total = 0.0;
    double altitude_error_total = 0.0;
    std::size_t altitude_error_count = 0U;
    for (const auto &sample : track.samples)
    {
        if (!sample.fields.position)
        {
            continue;
        }
        const PlanActualDeviation deviation = make_deviation(
            plan.mission_waypoints, cumulative_distances, plan.route_distance_m, sample);
        cross_track_total += deviation.cross_track_error_m;
        ++aggregate.compared_samples;
        if (!aggregate.max_cross_track_error_m ||
            deviation.cross_track_error_m > *aggregate.max_cross_track_error_m)
        {
            aggregate.max_cross_track_error_m = deviation.cross_track_error_m;
            aggregate.max_cross_track_error_time_s = sample.time_s;
        }
        if (deviation.altitude_error_m)
        {
            const double abs_error = std::abs(*deviation.altitude_error_m);
            altitude_error_total += abs_error;
            ++altitude_error_count;
            if (!aggregate.max_altitude_error_m || abs_error > *aggregate.max_altitude_error_m)
            {
                aggregate.max_altitude_error_m = abs_error;
                aggregate.max_altitude_error_time_s = sample.time_s;
            }
        }
        if (deviation.progress.along_route_m > aggregate.route_completion_m)
        {
            aggregate.route_completion_m = deviation.progress.along_route_m;
            aggregate.route_completion_ratio = deviation.progress.route_completion_ratio;
        }
    }
    if (aggregate.compared_samples > 0U)
    {
        aggregate.average_cross_track_error_m =
            cross_track_total / static_cast<double>(aggregate.compared_samples);
    }
    if (altitude_error_count > 0U)
    {
        aggregate.average_altitude_error_m =
            altitude_error_total / static_cast<double>(altitude_error_count);
    }
    if (const auto current_sample = track_sample_at(track, current_time_s))
    {
        if (current_sample->fields.position)
        {
            aggregate.current = make_deviation(plan.mission_waypoints,
                                               cumulative_distances,
                                               plan.route_distance_m,
                                               *current_sample);
        }
    }
    aggregate.route_completion_ratio = std::clamp(aggregate.route_completion_ratio, 0.0, 1.0);
    return aggregate;
}

std::optional<PlanActualDeviation>
plan_actual_deviation_at(const PlanVisualizationData &plan,
                         const animus::telemetry_core::TelemetrySample &sample)
{
    if (!sample.fields.position || plan.mission_waypoints.size() < 2U ||
        plan.route_distance_m <= 0.0)
    {
        return std::nullopt;
    }
    const std::vector<double> cumulative_distances =
        cumulative_route_distances(plan.mission_waypoints);
    return make_deviation(
        plan.mission_waypoints, cumulative_distances, plan.route_distance_m, sample);
}

GhostReplayCurrentRunSummary ghost_replay_current_run_summary(const PlanActualAggregate &comparison)
{
    GhostReplayCurrentRunSummary summary;
    summary.selected_track_m = comparison.selected_track_m;
    summary.route_completion_ratio = comparison.route_completion_ratio;
    summary.max_cross_track_error_m = comparison.max_cross_track_error_m;
    summary.max_altitude_error_m = comparison.max_altitude_error_m;
    return summary;
}

PlanRouteProfileSummary
plan_route_profile_summary(const PlanVisualizationData &plan,
                           const std::optional<PlanActualAggregate> &comparison)
{
    PlanRouteProfileSummary summary;
    summary.plan_loaded = true;
    summary.route_distance_m = plan.route_distance_m;
    summary.clearance_profile_status = "unavailable";
    if (!comparison)
    {
        return summary;
    }
    summary.telemetry_compared = comparison->compared_samples > 0U;
    summary.route_completion_ratio = comparison->route_completion_ratio;
    summary.compared_samples = comparison->compared_samples;
    summary.average_cross_track_error_m = comparison->average_cross_track_error_m;
    summary.max_cross_track_error_m = comparison->max_cross_track_error_m;
    if (comparison->current)
    {
        summary.active_from_waypoint_index = comparison->current->progress.from_waypoint_index;
        summary.active_to_waypoint_index = comparison->current->progress.to_waypoint_index;
        summary.current_altitude_error_m = comparison->current->altitude_error_m;
    }
    return summary;
}

} // namespace animus::app
