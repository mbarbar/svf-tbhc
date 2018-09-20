//===- ICFGStat.h ----------------------------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2018>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//
/*
 * ICFGStat.h
 *
 *  Created on: 12Sep.,2018
 *      Author: yulei
 */

#ifndef INCLUDE_UTIL_ICFGSTAT_H_
#define INCLUDE_UTIL_ICFGSTAT_H_

#include "Util/PTAStat.h"
#include "Util/ICFG.h"

class ICFGStat : public PTAStat {

private:
	ICFG* icfg;
    int numOfNodes;
    int numOfCallNodes;
    int numOfRetNodes;
    int numOfEntryNodes;
    int numOfExitNodes;
    int numOfIntraNodes;
    int numOfEdges;
    int numOfCallEdges;
    int numOfRetEdges;
    int numOfIntraEdges;

public:
    typedef std::set<const ICFGNode*> ICFGNodeSet;

    ICFGStat(ICFG* cfg) : PTAStat(NULL), icfg(cfg){
        numOfNodes = 0;
        numOfCallNodes = 0;
        numOfRetNodes = 0;
        numOfEntryNodes = 0;
        numOfExitNodes = 0;
        numOfIntraNodes = 0;
        numOfEdges = 0;
        numOfCallEdges = 0;
        numOfRetEdges = 0;
        numOfIntraEdges = 0;
	}

    void performStat(){

		ICFG::ICFGNodeIDToNodeMapTy::iterator it = icfg->begin();
		ICFG::ICFGNodeIDToNodeMapTy::iterator eit = icfg->end();
		for (; it != eit; ++it) {
			numOfNodes++;
			ICFGNode* node = it->second;

			if (SVFUtil::isa<IntraBlockNode>(node))
				numOfIntraNodes++;
			else if (SVFUtil::isa<CallBlockNode>(node))
				numOfIntraNodes++;

			/// add your code here to stat nodes and edges
		}

        PTNumStatMap["IntraBlockNode"] = numOfIntraNodes;

        std::cout << "\n*******ICFG Stat*******\n";
        printStat();
    }


};



#endif /* INCLUDE_UTIL_ICFGSTAT_H_ */