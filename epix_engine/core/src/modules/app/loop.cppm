module;

export module epix.core:app.loop;

import :app.decl;

namespace core {
export struct AppExit {
    int code = 0;
};
export struct LoopPlugin {
    void build(App& app);
};
}  // namespace core