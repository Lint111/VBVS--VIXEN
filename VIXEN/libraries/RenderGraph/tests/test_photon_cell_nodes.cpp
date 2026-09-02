// C0/C1 witness: each photon node is constructible without the application
// graph builder, proving the cache behavior is pluggable at node boundaries.

#include <gtest/gtest.h>

#include "Nodes/PhotonCellParamsConfigNode.h"
#include "Nodes/PhotonCellPassNodes.h"
#include "Nodes/PhotonCellTableNode.h"

using namespace Vixen::RenderGraph;

TEST(PhotonCellNodes, EachNodeCanBeComposedOutsideTheApplication) {
    PhotonCellTableNodeType tableType;
    PhotonCellParamsConfigNodeType paramsType;
    PhotonCellDepositNodeType depositType;
    PhotonCellFoldNodeType foldType;
    PhotonCellClearNodeType clearType;

    EXPECT_NE(tableType.CreateInstance("alternate_graph_table"), nullptr);
    EXPECT_NE(paramsType.CreateInstance("alternate_graph_params"), nullptr);
    EXPECT_NE(depositType.CreateInstance("alternate_graph_deposit"), nullptr);
    EXPECT_NE(foldType.CreateInstance("alternate_graph_fold"), nullptr);
    EXPECT_NE(clearType.CreateInstance("alternate_graph_clear"), nullptr);
}

TEST(PhotonCellNodes, ContractsStayAtNodeBoundary) {
    EXPECT_EQ(PhotonCellTableNode::kTableBytes, 8u * 1024u * 1024u);
    EXPECT_STREQ(PhotonCellDepositNode::Metadata::PROGRAM_NAME, "PhotonDeposit");
    EXPECT_STREQ(PhotonCellFoldNode::Metadata::PROGRAM_NAME, "PhotonCellFold");
    EXPECT_STREQ(PhotonCellClearNode::Metadata::PROGRAM_NAME, "PhotonCellClear");
    EXPECT_STREQ(PhotonCellDepositNode::MEMBERS[0].name, "HitRecordBuffer");
    EXPECT_STREQ(PhotonCellDepositNode::MEMBERS[1].name, "PhotonCellTable");
}
