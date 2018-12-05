//===- VTGraph.h -- Offline constraint graph -----------------------------//

/*
 * VTGraph.h
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#ifndef VTGRAPH_H
#define VTGRAPH_H

#include <tuple>

#include "MemoryModel/OfflineConsG.h"
#include "MemoryModel/CHA.h"
#include "MemoryModel/PointerAnalysis.h"

/*!
 * Offline constraint graph for Andersen's analysis.
 * In OCG, a 'ref' node is used to represent the point-to set of a constraint node.
 * 'Nor' means a constraint node of its corresponding ref node.
 */
class VTGraph: public OfflineConsG {
private:
    // What prefixes every class (type) name in LLVM.
    static const std::string CLASS_NAME_PREFIX;

    // Maps types to the new nodes generated to replace
    // all the memory objects of that type.
    std::map<const Type *, NodeID> typeToNode;

    // Maps a field (declarer + offset) to the node which
    // will represent that field.
    std::map<std::tuple<std::string, u32_t>, NodeID> fieldRepresentationMap;

    // For some queries
    CHGraph *chg;

public:
    VTGraph(PAG *p, SVFModule svfModule) : OfflineConsG(p) {
        chg = PointerAnalysis::getCHGraph();
    }

    // Replaces memory objects with type objects.
    void removeMemoryObjectNodes(void);

    // Collapses all fields to a single node.
    // Nodes referring to field f collapse to node X::f
    // where X is the class declaring f.
    void collapseFields(void);

    /// Dump the VT graph.
    virtual void dump(std::string name) override;

    static std::string getClassNameFromPointerType(const Type *type);
    static std::string getClassNameFromStructType(const StructType *structType);
    static const Type *dereferencePointerType(const PointerType *pt);

private:

    std::string getFieldDeclarer(std::string accessingClass, const StructType *ptrType, u32_t fieldOffset);
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
