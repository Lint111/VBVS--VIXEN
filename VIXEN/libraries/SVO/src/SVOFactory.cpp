#include "pch.h"
#include "ISVOStructure.h"
#include "LaineKarrasOctree.h"

namespace SVO {

std::unique_ptr<ISVOBuilder> SVOFactory::createBuilder(Type type) {
    switch (type) {
        case Type::LaineKarrasOctree:
            return std::make_unique<LaineKarrasBuilder>();

        default:
            return nullptr;
    }
}

std::unique_ptr<ISVOStructure> SVOFactory::createStructure(Type type) {
    switch (type) {
        case Type::LaineKarrasOctree:
            return std::make_unique<LaineKarrasOctree>();

        default:
            return nullptr;
    }
}

} // namespace SVO
