#include "Core/ITaskProfile.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Vixen::RenderGraph {

void ITaskProfile::SaveState(nlohmann::json& j) const {
    j["name"] = name_;
    j["category"] = category_;
    j["typeName"] = GetTypeName();
    j["workUnits"] = workUnits_;
    j["minWorkUnits"] = minWorkUnits_;
    j["maxWorkUnits"] = maxWorkUnits_;
    j["priority"] = priority_;
    j["workUnitType"] = static_cast<int>(workUnitType_);
    j["sampleCount"] = sampleCount_;
    j["lastMeasuredCostNs"] = lastMeasuredCostNs_;
    j["peakMeasuredCostNs"] = peakMeasuredCostNs_;
    j["isCalibrated"] = isCalibrated_;
    // Note: profileId_ not saved - regenerated on load
}

void ITaskProfile::LoadState(const nlohmann::json& j) {
    // Legacy support: "taskId" maps to name_
    if (j.contains("name")) name_ = j["name"];
    else if (j.contains("taskId")) name_ = j["taskId"];
    if (j.contains("category")) category_ = j["category"];
    if (j.contains("workUnits")) workUnits_ = j["workUnits"];
    if (j.contains("minWorkUnits")) minWorkUnits_ = j["minWorkUnits"];
    if (j.contains("maxWorkUnits")) maxWorkUnits_ = j["maxWorkUnits"];
    if (j.contains("priority")) priority_ = j["priority"];
    if (j.contains("workUnitType")) workUnitType_ = static_cast<WorkUnitType>(j["workUnitType"].get<int>());
    if (j.contains("sampleCount")) sampleCount_ = j["sampleCount"];
    if (j.contains("lastMeasuredCostNs")) lastMeasuredCostNs_ = j["lastMeasuredCostNs"];
    if (j.contains("peakMeasuredCostNs")) peakMeasuredCostNs_ = j["peakMeasuredCostNs"];
    if (j.contains("isCalibrated")) isCalibrated_ = j["isCalibrated"];

    // Clamp workUnits to bounds after loading
    workUnits_ = std::clamp(workUnits_, minWorkUnits_, maxWorkUnits_);
}

} // namespace Vixen::RenderGraph
