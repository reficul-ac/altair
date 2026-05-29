#include "workspace_layout_model.hpp"

#include <algorithm>

namespace animus::app
{

WorkspaceLayoutTransition
workspace_layout_for_switch(std::map<std::string, AppWorkspaceLayout> &layouts,
                            const std::string_view workspace_id)
{
    WorkspaceLayoutTransition result;
    result.workspace_id = canonical_workspace_id(workspace_id);
    if (result.workspace_id.empty())
    {
        result.workspace_id = "fly_test";
    }
    const auto existing = layouts.find(result.workspace_id);
    if (existing != layouts.end())
    {
        result.layout = existing->second;
        return result;
    }
    result.layout = default_workspace_layout(result.workspace_id);
    layouts[result.workspace_id] = result.layout;
    result.created_default = true;
    return result;
}

WorkspaceLayoutTransition reset_workspace_layout(std::map<std::string, AppWorkspaceLayout> &layouts,
                                                 const std::string_view workspace_id)
{
    WorkspaceLayoutTransition result;
    result.workspace_id = canonical_workspace_id(workspace_id);
    if (result.workspace_id.empty())
    {
        result.workspace_id = "fly_test";
    }
    result.layout = default_workspace_layout(result.workspace_id);
    layouts[result.workspace_id] = result.layout;
    result.created_default = true;
    return result;
}

SafeViewportRect safe_viewport_rect(const SafeViewportInput &input)
{
    SafeViewportRect rect;
    rect.x = input.margin + std::max(0.0F, input.left_drawer_width);
    rect.y = input.margin + std::max(0.0F, input.top_bar_height);
    const float right = std::max(
        rect.x, input.window_width - input.margin - std::max(0.0F, input.right_inspector_width));
    const float bottom = std::max(
        rect.y, input.window_height - input.margin - std::max(0.0F, input.bottom_drawer_height));
    rect.width = std::max(0.0F, right - rect.x);
    rect.height = std::max(0.0F, bottom - rect.y);
    return rect;
}

} // namespace animus::app
