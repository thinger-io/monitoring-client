#include <nlohmann/json.hpp>

class ThingerMonitorRestore {

protected:
    ThingerMonitorConfig config_;
    std::string name_;
    std::string tag_;

    ThingerMonitorRestore(ThingerMonitorConfig& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

public:

    virtual json download() = 0;
    virtual json restore() = 0;
    virtual json clean() = 0;

    virtual ~ThingerMonitorRestore() = default;

};
