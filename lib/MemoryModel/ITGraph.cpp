//===- ITGraph.cpp -- Constraint graph for ITC -----------------------------//

/*
 * ITGraph.cpp
 *
 *  Created on: Jan 16, 2018
 *      Author: Mohamad Barbar
 */

#include <queue>

#include "MemoryModel/ITGraph.h"
#include "WPA/VTAnalysis.h"

void ITGraph::buildCompatibleTypesMap(SVFModule svfModule) {
    // Perform VTA.
    VTAnalysis vt(true, true);
    vt.analyze(svfModule);

    // From results of VTA, realise what types can be assigned to what.
    std::map<const Type *, std::set<const Type *>> typeToCompatibleSet;
    std::map<const Type *, std::set<const Type *>> adjacency;
    std::set<const Type *> types;
    for (PAG::iterator idNodePairI = pag->begin(); idNodePairI != pag->end(); ++idNodePairI) {
        NodeID nodeId = idNodePairI->first;
        if (ValPN *vNode = SVFUtil::dyn_cast<ValPN>(idNodePairI->second)) {
            if (vNode->getType() == NULL) continue;  // TODO: why?

            // Dereference the vNode's type to match the objects'.
            const Type *vNodeType = vNode->getType();
            while (vNodeType->isPointerTy()) vNodeType = vNodeType->getPointerElementType();
            types.insert(vNodeType);

            std::string type_str;
            llvm::raw_string_ostream rso(type_str);
            vNodeType->print(rso);
            if (type_str == "i8") {
                // TODO: are void * an issue?
            }

            PointsTo& pts = vt.getPts(nodeId);
            // If two points-to sets share a type, ALL types in those two sets are compatible.
            // We're going to merge sets based on whether they share a type. Method is
            // essentially finding connected components.
            for (PointsTo::iterator typeObj1I = pts.begin(); typeObj1I != pts.end(); ++typeObj1I) {
                const ObjPN* typeObj1 = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(*typeObj1I));
                assert(typeObj1 && "non-obj node in points-to set");
                const Type *type1 = typeObj1->getType();
                if (type1 == NULL) continue;  // TODO: why?
                types.insert(type1);

                // Start inner iteration at typeObj1I since we set adjacency in both directions.
                for (PointsTo::iterator typeObj2I = typeObj1I; typeObj2I != pts.end(); ++typeObj2I) {
                    const ObjPN* typeObj2 = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(*typeObj2I));
                    assert(typeObj2 && "non-obj node in points-to set");
                    const Type *type2 = typeObj2->getType();
                    if (type2 == NULL) continue;  // TODO: why?
                    types.insert(type2);

                    adjacency[type1].insert(type2);
                    adjacency[type1].insert(vNodeType);
                    adjacency[type2].insert(type1);
                    adjacency[type2].insert(vNodeType);
                }
            }
        }
    }

    // Solve the connected components.
    std::map<const Type *, std::set<const Type *>> typeToConnectedComponent;
    std::set<std::set<const Type *>> connectedComponents;
    std::set<const Type *> visited;
    std::queue<const Type *> typesTodo;

    // Insert all the types into the queue. All the types in a connected component will be processed
    // by typesTodo.push performed during processing, and the ones inserted here will be ignored
    // by checking visited. This is so types in separate connected components are processed.
    for (std::set<const Type *>::const_iterator typeI = types.begin(); typeI != types.end(); ++typeI) {
        typesTodo.push(*typeI);
    }

    while (!typesTodo.empty()) {
        const Type *type = typesTodo.front();
        typesTodo.pop();

        // Don't process the type twice.
        if (visited.find(type) != visited.end()) continue;
        visited.insert(type);

        std::set<const Type *> connectedComponent = typeToConnectedComponent[type];
        // In case it was just made.
        typeToConnectedComponent[type].insert(type);
        connectedComponents.insert(connectedComponent);

        for (std::set<const Type *>::iterator dstTypeI = adjacency[type].begin(); dstTypeI != adjacency[type].end(); ++dstTypeI) {
            const Type *dstType = *dstTypeI;
            typeToConnectedComponent[dstType] = connectedComponent;
            connectedComponent.insert(dstType);
            typesTodo.push(dstType);
        }
    }

    // We have connected components, make elements compatible with each other.
    for (std::set<std::set<const Type *>>::iterator ccI = connectedComponents.begin(); ccI != connectedComponents.end(); ++ccI) {
        for (std::set<const Type *>::iterator t1I = ccI->begin(); t1I != ccI->end(); ++t1I) {
            // Start at t1I since we're setting compatibility in both directions.
            for (std::set<const Type *>::iterator t2I = t1I; t2I != ccI->end(); ++t2I) {
                compatibleTypes[*t1I][*t2I] = compatibleTypes[*t2I][*t1I] = true;
            }
        }
    }
}

void ITGraph::initialITC(void) {
    std::set<const ObjPN *> objects;
    std::map<const Type *, int> typeCounts;

    // Read FI objects from PAG, and extract relevant types.
    for (PAG::const_iterator nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        if (const FIObjPN* fiObj = SVFUtil::dyn_cast<FIObjPN>(nodeI->second)) {
            objects.insert(fiObj);
            typeCounts[fiObj->getType()] += 1;
        }
    }

    std::vector<const Type *> types;
    for (std::map<const Type *, int>::const_iterator typeCountI = typeCounts.begin(); typeCountI != typeCounts.end(); ++typeCountI) {
        types.push_back(typeCountI->first);
    }

    // Sort types according to how common they are in the PAG amongst FI objects.
    std::sort(types.begin(), types.end(),
              [&typeCounts](const Type *t1, const Type *t2) { return typeCounts[t1] > typeCounts[t2]; });

    // Make single-type blueprints (i.e. not collapsed types).
    std::vector<Blueprint> blueprints;
    for (std::vector<const Type *>::const_iterator typeI = types.begin(); typeI != types.end(); ++typeI) {
        Blueprint bp { *typeI };
        blueprints.push_back(bp);
    }

    // Collapse types into blueprints.
    for (std::vector<Blueprint>::iterator bp1I = blueprints.begin(); bp1I != blueprints.end(); ++bp1I) {
        if (bp1I->empty()) continue;

        // Start iteration after bp1I because the previous blueprints were tested against everything already.
        for (std::vector<Blueprint>::iterator bp2I = bp1I + 1; bp2I != blueprints.end(); ++bp2I) {
            if (bp2I->empty()) continue;

            bool ics = incompatibleBlueprints(*bp1I, *bp2I);
            if (ics) {
                // Blueprints are incompatible and can be collapsed. Move bp2 types to bp1.
                bp1I->insert(bp2I->begin(), bp2I->end());
                bp2I->clear();
            }
        }
    }

    // Remove the empty blueprints.
    blueprints.erase(std::remove_if(blueprints.begin(), blueprints.end(), [](Blueprint bp){ return bp.empty(); }),
                     blueprints.end());

    // Map types to their blueprint.
    for (std::vector<Blueprint>::const_iterator bpI = blueprints.begin(); bpI != blueprints.end(); ++bpI) {
        for (Blueprint::const_iterator typeI = bpI->begin(); typeI != bpI->end(); ++typeI) typeToBlueprint[*typeI] = *bpI;
    }

    // Do the collapsing.
    for (std::set<const ObjPN *>::const_iterator objI = objects.begin(); objI != objects.end(); ++objI) {
        findIncompatibleNodeForObj((*objI)->getId());
    }

    // Remove addr edges from actual objects, and add them to the new incompatible object nodes.
    for (std::set<IncompatibleObjPN *>::iterator instanceI = instances.begin(); instanceI != instances.end(); ++instanceI) {
        std::set<const ObjPN *> objs = (*instanceI)->getObjectNodes();
        std::set<AddrCGEdge *> addrEdgesToRemove;

        for (std::set<const ObjPN *>::const_iterator objI = objs.begin(); objI != objs.end(); ++objI) {
            ConstraintNode *cNode = getConstraintNode((*objI)->getId());
            for (ConstraintEdge::ConstraintEdgeSetTy::iterator edgeI = cNode->getOutEdges().begin(); edgeI != cNode->getOutEdges().end(); ++edgeI) {
                if (AddrCGEdge *addrEdge = SVFUtil::dyn_cast<AddrCGEdge>(*edgeI)) addrEdgesToRemove.insert(addrEdge);
            }
        }

        // Remove obj->X, and add IncompatibleObj->X.
        for (std::set<AddrCGEdge *>::iterator addrEdgeI = addrEdgesToRemove.begin(); addrEdgeI != addrEdgesToRemove.end(); ++addrEdgeI) {
            NodeID dstId = (*addrEdgeI)->getDstID();
            ConstraintNode *oldSrcNode = (*addrEdgeI)->getSrcNode();

            removeAddrEdge(*addrEdgeI);
            removeConstraintNode(oldSrcNode);
            addAddrCGEdge((*instanceI)->getId(), dstId);
        }
    }

    llvm::outs() << "Max type: " << *types[0] << " = " << typeCounts[types[0]] << " bp: " << typeToBlueprint[types[0]].size() << "\n";
    llvm::outs() << "2nd type: " << *types[1] << " = " << typeCounts[types[1]] << " bp: " << typeToBlueprint[types[1]].size() <<  "\n";
    llvm::outs() << "3rd type: " << *types[2] << " = " << typeCounts[types[2]] << " bp: " << typeToBlueprint[types[2]].size() << "\n";
    llvm::outs() << "4th type: " << *types[3] << " = " << typeCounts[types[3]] << " bp: " << typeToBlueprint[types[3]].size() << "\n";
    llvm::outs() << "5th type: " << *types[4] << " = " << typeCounts[types[4]] << " bp: " << typeToBlueprint[types[4]].size() << "\n";
    llvm::outs() << "Types built:      " << types.size()               << "\n";
    llvm::outs() << "Blueprints:       " << blueprints.size()          << "\n";
    llvm::outs() << "Total FI objects: " << objects.size()             << "\n";
    llvm::outs() << "instances:        " << instances.size()           << "\n";
    llvm::outs() << "instanceToB:      " << instanceToBlueprint.size() << "\n";
    llvm::outs().flush();
}

NodeID ITGraph::findIncompatibleNodeForObj(NodeID objId) {
    IncompatibleObjPN *instance;

    const ObjPN *obj = SVFUtil::dyn_cast<ObjPN>(pag->getPAGNode(objId));
    assert(obj && "trying to collapse non-object node");
    const Type *type = obj->getType();

    if (collapsedObjects.find(objId) != collapsedObjects.end()) {
        return collapsedObjects[objId];
    }

    if (instancesNeed[type].empty()) {
        // Initialise an instance.
        NodeID incompatibleObjNodeId = pag->addDummyIncompatibleObjNode();
        addConstraintNode(new ConstraintNode(incompatibleObjNodeId), incompatibleObjNodeId);

        instance = SVFUtil::dyn_cast<IncompatibleObjPN>(pag->getPAGNode(incompatibleObjNodeId));
        assert(instance && "Could not get created PAG node.");
        instances.insert(instance);

        instance->addObjectNode(obj);
        instanceToBlueprint[instance] = typeToBlueprint[type];

        Blueprint blueprint = instanceToBlueprint[instance];
        for (Blueprint::const_iterator bTypeI = blueprint.begin(); bTypeI != blueprint.end(); ++bTypeI) {
            // "Notify" all the types which can join this blueprint.
            instancesNeed[*bTypeI].push_back(instance);
        }

        // This instance doesn't need a 'type' object anymore. clear() is okay because it was initially empty.
        instancesNeed[type].clear();
    } else {
        instance = instancesNeed[type].back();
        instance->addObjectNode(obj);
        // Object of type type can no longer join so pop it.
        instancesNeed[type].pop_back();

        if (instance->objectNodeCount() == instanceToBlueprint[instance].size()) {
            // Instance is full!
            instanceToBlueprint.erase(instance);
        }
    }

    collapsedObjects[objId] = instance->getId();
    return instance->getId();
}

bool ITGraph::incompatibleTypes(const Type *t1, const Type *t2) {
    if (t1 == t2) return false;
    return !compatibleTypes[t1][t2];
}

bool ITGraph::incompatibleBlueprints(const Blueprint b1, const Blueprint b2) {
    if (b1.empty() || b2.empty()) return true;
    for (Blueprint::const_iterator b1I = b1.begin(); b1I != b1.end(); ++b1I) {
        for (Blueprint::const_iterator b2I = b2.begin(); b2I != b2.end(); ++b2I) {
            if (!incompatibleTypes(*b1I, *b2I)) return false;
        }
    }

    return true;
}

