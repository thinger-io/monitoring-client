#include <nlohmann/json.hpp>

class ThingerMonitorBackup {

protected:
    ThingerMonitorConfig config_;
    std::string name_;
    std::string tag_;

    ThingerMonitorBackup(ThingerMonitorConfig& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

public:

    virtual json backup() = 0;
    virtual json upload() = 0;
    virtual json clean() = 0;

    virtual ~ThingerMonitorBackup() = default;

};
