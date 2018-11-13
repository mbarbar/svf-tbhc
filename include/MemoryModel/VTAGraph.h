//===- VTAGraph.h -- Offline constraint graph -----------------------------//

/*
 * VTAGraph.h
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#ifndef VTAGRAPH_H
#define VTAGRAPH_H

#include "MemoryModel/OfflineConsG.h"

/*!
 * Offline constraint graph for Andersen's analysis.
 * In OCG, a 'ref' node is used to represent the point-to set of a constraint node.
 * 'Nor' means a constraint node of its corresponding ref node.
 */
class VTAGraph: public OfflineConsG {
private:
    // Maps types to the new nodes generated to replace
    // all the memory objects of that type.
    std::map<const Type *, NodeID> typeToNode;

public:
    VTAGraph(PAG *p) : OfflineConsG(p) {
    }

    void removeMemoryObjectNodes(void);

private:
    // Returns the set of FI Object nodes in the graph.
    std::set<NodeID> getFIObjectNodes(void);
};

namespace llvm {
/* !
 * GraphTraits specializations for the generic graph algorithms.
 * Provide graph traits for traversing from a constraint node using standard graph traversals.
 */

template<> struct GraphTraits<VTAGraph*> : public GraphTraits<GenericGraph<ConstraintNode,ConstraintEdge>* > {
typedef ConstraintNode *NodeRef;
};

}

#endif // VTAGRAPH_H
