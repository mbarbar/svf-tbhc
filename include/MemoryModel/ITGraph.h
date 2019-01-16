//===- ITGraph.h -- Constraint graph for ITC -----------------------------//

/*
 * ITGraph.h
 *
 *  Created on: Jan 16, 2018
 *      Author: Mohamad Barbar
 */

#ifndef ITGRAPH_H
#define ITGRAPH_H

#include "MemoryModel/CHA.h"
#include "MemoryModel/ConsG.h"
#include "MemoryModel/PointerAnalysis.h"

/// Constraint graph which collapses objects into Incompatible Object Nodes.
class ITGraph : public ConstraintGraph {
private:
    typedef std::set<const Type *> Blueprint;

    /// Maps types to the Blueprint they are contained in.
    std::map<const Type *, Blueprint>                        typeToBlueprint;
    /// Maps types to all the instances waiting for such a type.
    std::map<const Type *, std::vector<IncompatibleObjPN *>> instancesNeed;
    /// Maps an instance to the blueprint it is building.
    std::map<IncompatibleObjPN *, Blueprint>                 instanceToBlueprint;
    /// Set of all instances that have been built (even if incomplete).
    std::set<IncompatibleObjPN *>                            instances;

    /// Maps a pair of types to whether they are compatible or not.
    /// compatibleTypes[t1][t2] == true means t1 and t2 are compatible.
    std::map<const Type *, std::map<const Type*, bool>> compatibleTypes;

    /// All objects that have been collapsed thus far mapped to the incompatibleNode
    /// which contains them.
    std::map<NodeID, NodeID> collapsedObjects;

    CHGraph *chg;

public:
    ITGraph(PAG *p, SVFModule svfModule) : ConstraintGraph(p) {
        chg = PointerAnalysis::getCHGraph();
        buildCompatibleTypesMap(svfModule);
        initialITC();
    }

private:
    /// Populates the compatibleTypes map.
    void buildCompatibleTypesMap(SVFModule svfModule);

    /// Perform ITC on initial constraint graph.
    void initialITC(void);

    /// Inserts objId into an incompatibleObject node (possibly making one).
    /// Does not change anything about objId itself.
    /// Returns the node it was inserted into.
    NodeID findIncompatibleNodeForObj(NodeID objId);

    /// Returns true if t1 and t2 are incompatible. Based on what
    /// buildCompatibleTypesMap had determined to be compatible.
    bool incompatibleTypes(const Type *t1, const Type *t2);

    /// Returns true if all types in b1 are incompatible with all
    /// types in b2.
    bool incompatibleBlueprints(const Blueprint b1, const Blueprint b2);
};

#endif  // ITGRAPH_H
