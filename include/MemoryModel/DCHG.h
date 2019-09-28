//===----- DCHG.h -- CHG using DWARF debug info ---------------------------//
// This is based upon CHA.h.

/*
 * DCHG.h
 *
 *  Created on: Aug 23, 2019
 *      Author: Mohamad Barbar
 */

#ifndef DCHG_H_
#define DCHG_H_

#include "MemoryModel/GenericGraph.h"
#include "Util/SVFModule.h"
#include "Util/WorkList.h"

class SVFModule;
class DCHNode;

class DCHEdge : public GenericEdge<DCHNode> {
public:
    typedef enum {
        INHERITANCE = 0x1, // inheritance relation
        INSTANCE    = 0x2, // template-instance relation
        FIRST_FIELD = 0x4  // src -ff-> dst => dst is first field of src
    } DCHEDGETYPE;

    typedef GenericNode<DCHNode, DCHEdge>::GEdgeSetTy DCHEdgeSetTy;

    DCHEdge(DCHNode *src, DCHNode *dst, DCHEDGETYPE et, GEdgeFlag k = 0)
        : GenericEdge<DCHNode>(src, dst, k) {
        edgeType = et;
    }

    DCHEDGETYPE getEdgeType() const {
        return edgeType;
    }

private:
    DCHEDGETYPE edgeType;
};

class DCHNode : public GenericNode<DCHNode, DCHEdge> {
public:
    typedef enum {
        PURE_ABSTRACT = 0x1,     // pure virtual abstract class
        MULTI_INHERITANCE = 0x2, // multi inheritance class
        TEMPLATE = 0x04,         // template class
        SCALAR = 0x08            // non-class scalar type
    } CLASSATTR;

    typedef std::vector<const Function*> FuncVector;

    DCHNode(const llvm::DIType *diType, NodeID i = 0, GNodeK k = 0)
        : GenericNode<DCHNode, DCHEdge>(i, k), vtable(NULL), flags(0) {
        this->diType = diType;
        if (diType->getRawName() != NULL) {
            typeName = diType->getName();
        }
    }

    ~DCHNode() { }

    std::string getName() const {
        return typeName;
    }

    /// Flags
    //@{
    inline void setFlag(CLASSATTR mask) {
        flags |= mask;
    }
    inline bool hasFlag(CLASSATTR mask) const {
        return (flags & mask) == mask;
    }
    //@}

    /// Attribute
    //@{
    inline void setPureAbstract() {
        setFlag(PURE_ABSTRACT);
    }
    inline void setMultiInheritance() {
        setFlag(MULTI_INHERITANCE);
    }
    inline void setTemplate() {
        setFlag(TEMPLATE);
    }
    inline void setScalar() {
        setFlag(SCALAR);
    }
    inline bool isPureAbstract() const {
        return hasFlag(PURE_ABSTRACT);
    }
    inline bool isMultiInheritance() const {
        return hasFlag(MULTI_INHERITANCE);
    }
    inline bool isTemplate() const {
        return hasFlag(TEMPLATE);
    }
    inline bool isScalar() const {
        return hasFlag(SCALAR);
    }
    //@}

    void addVirtualFunctionVector(FuncVector vfuncvec) {
        virtualFunctionVectors.push_back(vfuncvec);
    }
    const std::vector<FuncVector> &getVirtualFunctionVectors() const {
        return virtualFunctionVectors;
    }
    void getVirtualFunctions(u32_t idx, FuncVector &virtualFunctions) const;

    const GlobalValue *getVTable() const {
        return vtable;
    }

    void setVTable(const GlobalValue *vtbl) {
        vtable = vtbl;
    }

    void addTypedef(const llvm::DIDerivedType *diTypedef) {
        typedefs.insert(diTypedef);
    }

    void addVirtualFunction(const Function *func, unsigned int virtualIndex) {
        primaryVTable.reserve(virtualIndex);
        primaryVTable[virtualIndex] = func;
    }

    const Function *getVirtualFunctionFromPrimaryVTable(unsigned int virtualIndex) {
        primaryVTable.reserve(virtualIndex);
        return primaryVTable[virtualIndex];
    }

private:
    /// Type of this node.
    const llvm::DIType *diType;
    /// Typedefs which map to this type.
    std::set<const llvm::DIDerivedType *> typedefs;
    const GlobalValue* vtable;
    std::string typeName;
    size_t flags;
    /// The virtual functions which this class actually defines/overrides.
    std::vector<const Function *> primaryVTable;
    /*
     * virtual functions inherited from different classes are separately stored
     * to model different vtables inherited from different fathers.
     *
     * Example:
     * class C: public A, public B
     * vtableC = {Af1, Af2, ..., inttoptr, Bg1, Bg2, ...} ("inttoptr"
     * instruction works as the delimiter for separating virtual functions
     * inherited from different classes)
     *
     * virtualFunctionVectors = {{Af1, Af2, ...}, {Bg1, Bg2, ...}}
     */
    std::vector<std::vector<const Function*>> virtualFunctionVectors;
};

/// Dwarf based CHG.
class DCHGraph : public GenericGraph<DCHNode, DCHEdge> {
public:
    DCHGraph(const SVFModule svfMod)
        : svfModule(svfMod) { //, classNum(0), vfID(0), buildingCHGTime(0) {
    }

    //~DCHGraph();

    /// Builds the CHG from DWARF debug information. extend determines
    /// whether to extend the CHG with first field edges.
    virtual void buildCHG(bool extend);

    void dump(const std::string& filename) {
        GraphPrinter::WriteGraphToFile(llvm::outs(), filename, this);
    }

protected:
    /// SVF Module this CHG is built from.
    SVFModule svfModule;
    /// Whether this CHG is an extended CHG (first-field). Set by buildCHG.
    bool extended = false;
    /// Maps DITypes to their nodes.
    std::map<const llvm::DIType *, DCHNode *> diTypeToNodeMap;
    /// Maps typedefs to their (potentially transitive) base type.
    std::map<const llvm::DIType *, DCHNode *> typedefToNodeMap;

private:
    /// Construction helper to process DIBasicTypes.
    void handleDIBasicType(const llvm::DIBasicType *basicType);
    /// Construction helper to process DICompositeTypes.
    void handleDICompositeType(const llvm::DICompositeType *compositeType);
    /// Construction helper to process DIDerivedTypes.
    void handleDIDerivedType(const llvm::DIDerivedType *derivedType);
    /// Construction helper to process DISubroutineTypes.
    void handleDISubroutineType(const llvm::DISubroutineType *subroutineType);

    /// Finds all defined virtual functions and attaches them to nodes.
    void buildVTables(void);

    /// Attaches the typedef(s) to the base node.
    void handleTypedef(const llvm::DIDerivedType *typedefType);

    /// Creates a node from type, or returns it if it exists.
    /// Only suitable for TODO.
    DCHNode *getOrCreateNode(const llvm::DIType *type);

    /// Creates an edge between from t1 to t2.
    DCHEdge *addEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::DCHEDGETYPE et);
    /// Returns the edge between t1 and t2 if it exists, returns NULL otherwise.
    DCHEdge *hasEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::DCHEDGETYPE et);
};


namespace llvm {
/* !
 * GraphTraits specializations for generic graph algorithms.
 * Provide graph traits for traversing from a constraint node using standard graph traversals.
 */
template<> struct GraphTraits<DCHNode*> : public GraphTraits<GenericNode<DCHNode,DCHEdge>*  > {
};

/// Inverse GraphTraits specializations for call graph node, it is used for inverse traversal.
template<>
struct GraphTraits<Inverse<DCHNode*> > : public GraphTraits<Inverse<GenericNode<DCHNode,DCHEdge>* > > {
};

template<> struct GraphTraits<DCHGraph*> : public GraphTraits<GenericGraph<DCHNode,DCHEdge>* > {
    typedef DCHNode *NodeRef;
};

}

#endif /* DCHG_H_ */

