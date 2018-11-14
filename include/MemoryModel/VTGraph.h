//===- VTGraph.h -- Offline constraint graph -----------------------------//

/*
 * VTGraph.h
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#ifndef VTGRAPH_H
#define VTGRAPH_H

#include "MemoryModel/OfflineConsG.h"

/*!
 * Offline constraint graph for Andersen's analysis.
 * In OCG, a 'ref' node is used to represent the point-to set of a constraint node.
 * 'Nor' means a constraint node of its corresponding ref node.
 */
class VTGraph: public OfflineConsG {
private:
    // Maps types to the new nodes generated to replace
    // all the memory objects of that type.
    std::map<const Type *, NodeID> typeToNode;

public:
    VTGraph(PAG *p) : OfflineConsG(p) {
    }

    void removeMemoryObjectNodes(void);

    /// Dump the VT graph.
    virtual void dump(std::string name) override;
};

namespace llvm {
/* !
 * GraphTraits specializations for the generic graph algorithms.
 * Provide graph traits for traversing from a constraint node using standard graph traversals.
 */

template<> struct GraphTraits<VTGraph*> : public GraphTraits<GenericGraph<ConstraintNode,ConstraintEdge>* > {
typedef ConstraintNode *NodeRef;
};

}

#endif // VTGRAPH_H
