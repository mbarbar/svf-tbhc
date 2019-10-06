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
#include "MemoryModel/CHG.h"
#include "Util/SVFModule.h"
#include "Util/WorkList.h"

class SVFModule;
class DCHNode;

class DCHEdge : public GenericEdge<DCHNode> {
public:
    enum {
        INHERITANCE, // inheritance relation
        INSTANCE,    // template-instance relation
        FIRST_FIELD  // src -ff-> dst => dst is first field of src
    };

    typedef GenericNode<DCHNode, DCHEdge>::GEdgeSetTy DCHEdgeSetTy;

    DCHEdge(DCHNode *src, DCHNode *dst, GEdgeFlag k = 0)
        : GenericEdge<DCHNode>(src, dst, k), offset(0) {
    }

    unsigned int getOffset(void) const {
        return offset;
    }

    void setOffset(unsigned int offset) {
        this->offset = offset;
    }

private:
    unsigned int offset;
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
        if (diType == NULL) {
            // TODO: this should not clash - needs some testing.
            typeName = "void";
        } else if (diType->getRawName() != NULL) {
            typeName = diType->getName();
        } else {
            // TODO: we can name this from the DIType properly.
            typeName = "unnamed!";
        }
    }

    ~DCHNode() { }

    llvm::DIType *getType(void) const {
        return diType;
    }

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

    void addTypedef(const llvm::DIDerivedType *diTypedef) {
        typedefs.insert(diTypedef);
    }

    const std::set<const llvm::DIDerivedType *> &getTypedefs(void) const {
        return typedefs;
    }

    void setVTable(const GlobalValue *vtbl) {
        vtable = vtbl;
    }

    const GlobalValue *getVTable() const {
        return vtable;
    }

    /// Returns the vector of virtual function vectors.
    const std::vector<std::vector<const Function *>> &getVfnVectors(void) const {
        return vfnVectors;
    }

    /// Return the nth virtual function vector in the vtable.
    std::vector<const Function *> &getVfnVector(unsigned n) {
        if (vfnVectors.size() < n + 1) {
            vfnVectors.resize(n + 1);
        }

        return vfnVectors[n];
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
    /// If a vtable is split into more than one vfn vector for multiple inheritance,
    /// 0 would be the primary base + this classes virtual functions, 1 would be
    /// the second parent, 2 would be third parent, etc.
    std::vector<std::vector<const Function*>> vfnVectors;
};

/// Dwarf based CHG.
class DCHGraph : public CommonCHGraph, public GenericGraph<DCHNode, DCHEdge> {
public:
    static const std::string tirMetadataName;

    DCHGraph(const SVFModule svfMod)
        : svfModule(svfMod), numTypes(0) { // vfID(0), buildingCHGTime(0) {
    }

    //~DCHGraph();

    /// Builds the CHG from DWARF debug information. extend determines
    /// whether to extend the CHG with first field edges.
    virtual void buildCHG(bool extend);

    void dump(const std::string& filename) {
        GraphPrinter::WriteGraphToFile(llvm::outs(), filename, this);
    }

    void print(void) const;

    const bool csHasVFnsBasedonCHA(CallSite cs) const override {
        llvm::DIType *type = getCSStaticType(cs);
        return getOrCreateNode(type)->getVTable != NULL;
    }

    const VFunSet &getCSVFsBasedonCHA(CallSite cs) const override;

    const bool csHasVtblsBasedonCHA(CallSite cs) const override {
        llvm::DIType *type = getCSStaticType(cs);
        return getOrCreateNode(type)->getVTable() != NULL;
    }

    const VTableSet &getCSVtblsBasedonCHA(CallSite cs) const override;
    void getVFnsFromVtbls(CallSite cs, VTableSet &vtbls, VFunSet &virtualFunctions) const override;

protected:
    /// SVF Module this CHG is built from.
    SVFModule svfModule;
    /// Whether this CHG is an extended CHG (first-field). Set by buildCHG.
    bool extended = false;
    /// Maps DITypes to their nodes.
    std::map<const llvm::DIType *, DCHNode *> diTypeToNodeMap;
    /// Maps typedefs to their (potentially transitive) base type.
    std::map<const llvm::DIType *, DCHNode *> typedefToNodeMap;
    /// Maps VTables to the DIType associated with them.
    std::map<const GlobalValue *, const llvm::DIType *> vtblToTypeMap;
    /// Maps types to all children (i.e. CHA).
    std::map<const llvm::DIType *, std::set<const DCHNode *>> chaMap;
    /// Maps types to a set with their vtable and all their children's.
    std::map<const llvm::DIType *, VTableSet> vtableCHAMap;
    /// Maps callsites to a set of potential virtual functions based on CHA.
    std::map<CallSite, VFunSet> csCHAMap;

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
    void buildVTables(const Module &module);

    /// Returns a set of all children of type (CHA). Also gradually builds chaMap.
    std::set<const DCHNode *> &cha(const llvm::DIType *type);

    /// Attaches the typedef(s) to the base node.
    void handleTypedef(const llvm::DIType *typedefType);

    /// Creates a node from type, or returns it if it exists.
    /// Only suitable for TODO.
    DCHNode *getOrCreateNode(const llvm::DIType *type);

    /// Retrieves the metadata associated with a *virtual* callsite.
    const llvm::DIType *getCSStaticType(CallSite cs) const {
        llvm::MDNode *md = cs.getInstruction()->hasMetadata(tirMetadataName);
        assert(md != nullptr && "Missing type metadata at virtual callsite");
        llvm::DIType *diType = llvm::dyn_cast<llvm::DIType>(md);
        assert(diType != nullptr && "Incorrect metadata type at virtual callsite");
        return diType;
    }

    /// Checks if a node exists for type.
    bool hasNode(const llvm::DIType *type) const {
        // Check, does the node for type exist?
        if (diTypeToNodeMap[type] != NULL) {
            return diTypeToNodeMap[type];
        }

        if (typedefToNodeMap[type] != NULL) {
            return typedefToNodeMap[type];
        }
    }

    /// Creates an edge between from t1 to t2.
    DCHEdge *addEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::GEdgeKind et);
    /// Returns the edge between t1 and t2 if it exists, returns NULL otherwise.
    DCHEdge *hasEdge(const llvm::DIType *t1, const llvm::DIType *t2, DCHEdge::GEdgeKind et);

    /// Number of types (nodes) in the graph.
    NodeID numTypes;
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

