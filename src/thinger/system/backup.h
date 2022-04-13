#include <nlohmann/json.hpp>

class ThingerMonitorBackup {

protected:

    ThingerMonitorBackup(ThingerMonitorConfig& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

    ThingerMonitorConfig config() const { return config_; }
    std::string name() const { return name_; }
    std::string tag() const { return tag_; }

private:

    ThingerMonitorConfig config_;
    std::string name_;
    std::string tag_;

public:

    virtual json backup() = 0;
    virtual json upload() = 0;
    virtual json clean() = 0;

    virtual ~ThingerMonitorBackup() = default;

};
