#ifndef CONFIGURATION_PROVIDER_HPP_
#define CONFIGURATION_PROVIDER_HPP_
namespace cly {

class ConfigurationProvider {
public:
    virtual ~ConfigurationProvider() = default;

    virtual bool isNetworkingEnabled() const = 0;
    virtual bool isTrackingEnabled() const = 0;
    virtual unsigned int getRequestQueueSizeLimit() const = 0;
};
} // namespace cly
#endif