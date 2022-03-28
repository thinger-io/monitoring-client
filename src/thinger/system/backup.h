
class ThingerMonitorBackup {

protected:
    ThingerMonitorConfig config_;
    std::string name_;
    std::string tag_;

    ThingerMonitorBackup(ThingerMonitorConfig& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

public:

    virtual void create() = 0;
    virtual int upload() = 0;
    virtual void clean() = 0;

    virtual ~ThingerMonitorBackup() = default;

};
