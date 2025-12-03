#include "ILoggable.h"
#include "Logger.h"

namespace Vixen::Log {

void ILoggable::InitializeLogger(const std::string& subsystemName, bool enabled) {
    logger = std::make_shared<Logger>(subsystemName, enabled);
}

void ILoggable::RegisterToParentLogger(Logger* parentLogger) {
    if (parentLogger && logger) {
        parentLogger->AddChild(logger);  // Pass shared_ptr for shared ownership
    }
}

void ILoggable::DeregisterFromParentLogger(Logger* parentLogger) {
    if (parentLogger && logger) {
        parentLogger->RemoveChild(logger.get());
    }
}

void ILoggable::SetLoggerEnabled(bool enabled) {
    if (logger) {
        logger->SetEnabled(enabled);
    }
}

void ILoggable::SetLoggerTerminalOutput(bool enabled) {
    if (logger) {
        logger->SetTerminalOutput(enabled);
    }
}

} // namespace Vixen::Log
