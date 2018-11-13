#include "WPA/VTAnalysis.h"

void VTAnalysis::validateTests() {
	for (u32_t i = 0; i < svfMod.getModuleNum(); ++i) {
		Module *module = svfMod.getModule(i);
		if (Function* checkFun = module->getFunction("checkType")) {
			if (!checkFun->use_empty())
				SVFUtil::outs() << "[" << this->PTAName() << "] Checking" << "\n";

			for (Value::user_iterator i = checkFun->user_begin(), e = checkFun->user_end(); i != e; ++i)
				if (SVFUtil::isa < CallInst > (*i) || SVFUtil::isa < InvokeInst > (*i)) {

                    CallSite cs(*i);
                    assert(cs.getNumArgOperands() == 2
                           && "arguments should be two pointers!!");
                    Value* v1 = cs.getArgOperand(0);
                    Value* v2 = cs.getArgOperand(1);
                    NodeID node1 = pag->getValueNode(v1);
                    NodeID node2 = pag->getValueNode(v2);
				}
		}
	}

}


VTAGraph* VTAnalysis::createVTAGraph(){
    /// Build Constraint Graph
    VTAGraph *vtg = new VTAGraph(pag);
    vtg->removeMemoryObjectNodes();
    vtg->dump("vta_graph");
    VSCC* vscc = new VSCC(vtg);
    vscc->find();

    NodeBS changedRepNodes;
    NodeStack & topoOrder = vscc->topoNodeStack();
    while (!topoOrder.empty()) {
        NodeID repNodeId = topoOrder.top();
        topoOrder.pop();
        // merge sub nodes to rep node
        mergeSccNodes(repNodeId, changedRepNodes);
    }

    // update rep/sub relation in the constraint graph.
    // each node will have a rep node
    for(NodeBS::iterator it = changedRepNodes.begin(), eit = changedRepNodes.end(); it!=eit; ++it)
        updateNodeRepAndSubs(*it);


    return vtg;
}
