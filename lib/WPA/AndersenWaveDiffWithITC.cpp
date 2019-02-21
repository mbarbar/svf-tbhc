//===- AndersenWaveDiffWithITC.cpp -- Wave diff propagation Andersen's analysis with ITC --//

/*
 * AndersenWaveDiffWithITC.cpp
 *
 *  Created on: 27/12/2018
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/CHA.h"
#include "MemoryModel/VTGraph.h"
#include "Util/CPPUtil.h"
#include "WPA/Andersen.h"

void AndersenWaveDiffWithITC::initialITC(void) {
    std::set<const FIObjPN *>   objects;
    std::map<const Type *, int> typeCounts;

    // Read objects from PAG, and extract relevant types.
    for (auto nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        const PAGNode *pagNode = nodeI->second;
        if (const FIObjPN* fiObj = SVFUtil::dyn_cast<FIObjPN>(pagNode)) {
            const Type *type = fiObj->getMemObj()->getType();
            // TODO: better checking, and will need to deal with scalar pointers.
            if (!VTGraph::hasNonScalarTypes(type)
                || cppUtil::getClassNameFromType(type) == "") {
                continue;
            }

            objects.insert(fiObj);
            typeCounts[type] += 1;
        }
    }

    std::vector<const Type *>   types;
    for (auto typeCountI = typeCounts.begin(); typeCountI != typeCounts.end(); ++typeCountI) {
        types.push_back(typeCountI->first);
    }

    std::sort(types.begin(), types.end(),
              [&typeCounts](const Type *t1, const Type *t2) { return typeCounts[t1] > typeCounts[t2]; });

    llvm::outs() << "Max type: " << *types[0] << " = " << typeCounts[types[0]] << "\n";
    llvm::outs() << "Snd type: " << *types[1] << " = " << typeCounts[types[1]] << "\n";

    std::vector<Blueprint> blueprints;

    // Make single-type blueprints (i.e. not collapsed types).
    for (auto typeI = types.begin(); typeI != types.end(); ++typeI) {
        Blueprint blueprint { *typeI };
        blueprints.push_back(blueprint);
    }

    // Collapse types into blueprints.
    for (auto aBlueprintI = blueprints.begin(); aBlueprintI != blueprints.end(); ++aBlueprintI) {
        if (aBlueprintI->empty()) continue;

        // Start second iteration after aBlueprintI because the previous blueprints were tested
        // against everything already.
        for (auto bBlueprintI = aBlueprintI + 1; bBlueprintI != blueprints.end(); ++bBlueprintI) {
            if (bBlueprintI->empty()) continue;

            bool ics = incompatibleBlueprints(*aBlueprintI, *bBlueprintI);
            if (ics) {
                // Blueprints are incompatible and can be collapsed.
                // Move 'b' types to 'a'.
                aBlueprintI->insert(bBlueprintI->begin(), bBlueprintI->end());
                bBlueprintI->clear();
            }
        }
    }

    // Remove the empty blueprints.
    blueprints.erase(std::remove_if(blueprints.begin(), blueprints.end(),
                                    [](Blueprint blueprint){ return blueprint.empty(); }),
                     blueprints.end());

    // Map types to their blueprint.
    for (auto blueprintI = blueprints.begin(); blueprintI != blueprints.end(); ++blueprintI) {
        for (auto typeI = blueprintI->begin(); typeI != blueprintI->end(); ++typeI) {
            typeToBlueprint[*typeI] = *blueprintI;
        }
    }

    std::set<Instance *> instances;

    // Do the collapsing.
    for (auto objI = objects.begin(); objI != objects.end(); ++objI) {
        const FIObjPN *obj = *objI;
        const Type *type = obj->getMemObj()->getType();

        if (instancesNeed[type].empty()) {
            // Initialise an instance.
            Instance *instance = new Instance;
            instance->insert(obj);
            instanceToBlueprint[instance] = typeToBlueprint[type];

            Blueprint blueprint = instanceToBlueprint[instance];
            for (auto bTypeI = blueprint.begin(); bTypeI != blueprint.end(); ++bTypeI) {
                // "Notify" all the types which can join this blueprint.
                instancesNeed[*bTypeI].push_back(instance);
            }

            // Clear the typeToInstance for type, because we just added it.
            // .clear() is okay because it had 0, then we added 1, so back to 0.
            instancesNeed[type].clear();
        } else {
            Instance *instance = instancesNeed[type].back();
            instance->insert(obj);
            // Object of type type can no longer join so pop it.
            instancesNeed[type].pop_back();

            if (instance->size() == instanceToBlueprint[instance].size()) {
                // Instance is full!
                instances.insert(instance);
                instanceToBlueprint.erase(instance);
            }
        }
    }

    llvm::outs() << "Types:       " << types.size()               << "\n";
    llvm::outs() << "Blueprints:  " << blueprints.size()          << "\n";
    llvm::outs() << "FI objects:  " << objects.size()             << "\n";
    llvm::outs() << "instances:   " << instances.size()           << "\n";
    llvm::outs() << "instanceToB: " << instanceToBlueprint.size() << "\n";
}

bool AndersenWaveDiffWithITC::incompatibleTypes(const Type *t1, const Type *t2) {
    // TODO: handle non-class pointer types
    if (t1 == t2) return true;

    std::pair<const Type *, const Type *> t1t2(t1, t2);
    if (incompatibleTypesMap.find(t1t2) != incompatibleTypesMap.end()) {
        return incompatibleTypesMap[t1t2];
    }

    std::pair<const Type *, const Type *> t2t1(t2, t1);
    // Don't need to check for t2t1 in the map since we always
    // add both into the map at the same time.

    std::string t1Name = cppUtil::getClassNameFromType(t1);
    t1Name = t1Name.substr(0, t1Name.find_first_of("."));
    t1Name = cppUtil::removeTemplatesFromName(t1Name);

    std::string t2Name = cppUtil::getClassNameFromType(t2);
    t2Name = t2Name.substr(0, t2Name.find_first_of("."));
    t2Name = cppUtil::removeTemplatesFromName(t2Name);

    if (t1Name == "" || t2Name == "") {
        // TODO: conservative?
        return false;
    }

    const CHNode *t1Node = chg->getNode(t1Name);
    const CHNode *t2Node = chg->getNode(t2Name);

    if (t1Node == NULL || t2Node == NULL) {
        // TODO: conservative?
        incompatibleTypesMap[t1t2] = false;
        incompatibleTypesMap[t2t1] = false;
        return false;
    }

    if (t1Node->getCCLabel() != t2Node->getCCLabel()) {
        // If they're part of separate connected components, they
        // cannot share the same parent.
        incompatibleTypesMap[t1t2] = true;
        incompatibleTypesMap[t2t1] = true;
        return true;
    }

    CHGraph::CHNodeSetTy t1Parents = chg->getAncestors(t1Name);
    t1Parents.insert(t1Node);
    CHGraph::CHNodeSetTy t2Parents = chg->getAncestors(t2Name);
    t2Parents.insert(t2Node);

    bool areDisjoint = disjoint(t1Parents, t2Parents);
    incompatibleTypesMap[t1t2] = areDisjoint;
    incompatibleTypesMap[t2t1] = areDisjoint;

    return areDisjoint;
}

bool AndersenWaveDiffWithITC::incompatibleBlueprints(const Blueprint b1, const Blueprint b2) {
    if (b1.empty() || b2.empty()) return true;

    bool incompatible = true;
    for (auto b1I = b1.begin(); b1I != b1.end(); ++b1I) {
        for (auto b2I = b2.begin(); b2I != b2.end(); ++b2I) {
            const Type *b1Type = (*b1I);
            const Type *b2Type = (*b2I);

            if (!incompatibleTypes(b1Type, b2Type)) {
                return false;
            }
        }
    }

    return true;
}

template <typename T>
bool AndersenWaveDiffWithITC::disjoint(const std::set<T> s1, const std::set<T> s2) {
    typename std::set<T>::const_iterator s1I   = s1.begin();
    typename std::set<T>::const_iterator s1End = s1.end();

    typename std::set<T>::const_iterator s2I   = s2.begin();
    typename std::set<T>::const_iterator s2End = s2.end();

    while (s1I != s1End && s2I != s2End) {
        if (*s1I < *s2End) {
            ++s1I;
        } else  {
            if (!(*s2I < *s1I)) {
                return false;
            }
            ++s2I;
        }
    }

    return true;
}

