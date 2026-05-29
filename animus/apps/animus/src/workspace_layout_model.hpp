#pragma once

#include "app_config.hpp"

#include <map>
#include <string>
#include <string_view>

namespace animus::app
{

struct WorkspaceLayoutTransition
{
    std::string workspace_id;
    AppWorkspaceLayout layout;
    bool created_default = false;
};

struct SafeViewportInput
{
    float window_width = 0.0F;
    float window_height = 0.0F;
    float top_bar_height = 0.0F;
    float left_drawer_width = 0.0F;
    float right_inspector_width = 0.0F;
    float bottom_drawer_height = 0.0F;
    float margin = 0.0F;
};

struct SafeViewportRect
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

[[nodiscard]] WorkspaceLayoutTransition
workspace_layout_for_switch(std::map<std::string, AppWorkspaceLayout> &layouts,
                            std::string_view workspace_id);

[[nodiscard]] WorkspaceLayoutTransition
reset_workspace_layout(std::map<std::string, AppWorkspaceLayout> &layouts,
                       std::string_view workspace_id);

[[nodiscard]] SafeViewportRect safe_viewport_rect(const SafeViewportInput &input);

} // namespace animus::app
