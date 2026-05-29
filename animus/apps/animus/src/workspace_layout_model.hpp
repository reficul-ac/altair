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

[[nodiscard]] WorkspaceLayoutTransition
workspace_layout_for_switch(std::map<std::string, AppWorkspaceLayout> &layouts,
                            std::string_view workspace_id);

[[nodiscard]] WorkspaceLayoutTransition
reset_workspace_layout(std::map<std::string, AppWorkspaceLayout> &layouts,
                       std::string_view workspace_id);

} // namespace animus::app
