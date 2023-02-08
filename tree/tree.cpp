#include "tree.h"

#include <cassert>
#include <utils/matrix.h>

using namespace std;

Tree::Tree(Params&& n_params, Node* n_root)
{
    params = std::move(n_params);
    root = n_root;
}

Tree::~Tree()
{
    // browse tree to delete all nodes
    stack<Node*> node_stack;
    node_stack.push(root);
    
    while (!node_stack.empty())
    {
        Node* node = node_stack.top();
        node_stack.pop();
        
        // add neighbors to the stack
        stack<Node*> delete_stack;
        delete_stack.push(node);
        Node* next_node;
        FOR_NEXT(node, next_node)
        {
            // add neighbor if it's existed
            if (next_node->neighbor) node_stack.push(next_node->neighbor);
            
            // push next_node into delete_stack
            delete_stack.push(next_node);
        }
        
        // delete all nodes in delete_stack
        while (!delete_stack.empty())
        {
            Node* delete_node = delete_stack.top();
            delete_stack.pop();
            
            // delete node
            delete delete_node;
        }
    }
    
}

void Tree::setupFunctionPointers()
{
    switch (aln.num_states) {
        case 2:
            updatePartialLhPointer = &Tree::updatePartialLhTemplate<4>;
            calculateSamplePlacementCostPointer = &Tree::calculateSamplePlacementCostTemplate<2>;
            calculateSubTreePlacementCostPointer = &Tree::calculateSubTreePlacementCostTemplate<2>;
            break;
        case 4:
            updatePartialLhPointer = &Tree::updatePartialLhTemplate<4>;
            calculateSamplePlacementCostPointer = &Tree::calculateSamplePlacementCostTemplate<4>;
            calculateSubTreePlacementCostPointer = &Tree::calculateSubTreePlacementCostTemplate<4>;
            break;
        case 20:
            updatePartialLhPointer = &Tree::updatePartialLhTemplate<4>;
            calculateSamplePlacementCostPointer = &Tree::calculateSamplePlacementCostTemplate<20>;
            calculateSubTreePlacementCostPointer = &Tree::calculateSubTreePlacementCostTemplate<20>;
            break;
            
        default:
            outError("Sorry! currently we only support DNA data!");
            break;
    }
}

void Tree::setupBlengthThresh()
{
    default_blength = 1.0 / aln.ref_seq.size();
    min_blength = params->min_blength_factor * default_blength;
    max_blength = params->max_blength_factor * default_blength;
    min_blength_mid = params->min_blength_mid_factor * default_blength;
    min_blength_sensitivity = min_blength * 1e-5;
    half_min_blength_mid = min_blength_mid * 0.5;
    half_max_blength = max_blength * 0.5;
    double_min_blength = min_blength + min_blength;
}

void Tree::setup()
{
    setupFunctionPointers();
    setupBlengthThresh();
}

string Tree::exportTreeString(bool binary, Node* node)
{
    // init starting node from root
    if (!node)
        node = root;
    
    // move to its neighbor
    if (node->neighbor)
        node = node->neighbor;
    
    // do something with its neighbor
    if (node->isLeave())
        return node->exportString(binary);
        
    string output = "(";
    bool add_comma = false;
    Node* next;
    FOR_NEXT(node, next)
    {
        if (!add_comma)
            add_comma = true;
        else
            output += ",";
        output += exportTreeString(binary, next);
    }
    string length = node->length < 0 ? "0" : convertDoubleToString(node->length);
    output += "):" + length;
    
    return output;
}

void Tree::updateMidBranchLh(Node* const node, const SeqRegions* const parent_upper_regions, stack<Node*> &node_stack, bool &update_blength)
{
    // update vector of regions at mid-branch point
    SeqRegions* mid_branch_regions = NULL;
    computeMidBranchRegions(node, mid_branch_regions, *parent_upper_regions);
    
    if (!mid_branch_regions)
        handleNullNewRegions(node, (node->length <= 1e-100), node_stack, update_blength, "inside updatePartialLh(), from parent: should not have happened since node->length > 0");
    // update likelihood at the mid-branch point
    else
        replacePartialLH(node->mid_branch_lh, mid_branch_regions);
    
    // delete mid_branch_regions
    if (mid_branch_regions) delete mid_branch_regions;
}

SeqRegions* Tree::computeUpperLeftRightRegions(Node* const next_node, Node* const node, const SeqRegions* const parent_upper_regions, stack<Node*> &node_stack, bool &update_blength)
{
    SeqRegions* upper_left_right_regions = NULL;
    SeqRegions* lower_regions = next_node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
    parent_upper_regions->mergeUpperLower(upper_left_right_regions, node->length, *lower_regions, next_node->length, aln, model, params->threshold_prob);
    
    // handle cases when new regions is null/empty
    if (!upper_left_right_regions || upper_left_right_regions->size() == 0)
        handleNullNewRegions(node, (node->length <= 0 && next_node->length <= 0), node_stack, update_blength, "Strange: None vector from non-zero distances in updatePartialLh() from parent direction.");
    
    return upper_left_right_regions;
}

void Tree::updateNewPartialIfDifferent(Node* const next_node, SeqRegions* &upper_left_right_regions, std::stack<Node*> &node_stack, const PositionType seq_length)
{
    if (next_node->getPartialLhAtNode(aln, model, params->threshold_prob)->areDiffFrom(*upper_left_right_regions, seq_length, aln.num_states, &params.value()))
    {
        replacePartialLH(next_node->partial_lh, upper_left_right_regions);
        node_stack.push(next_node->neighbor);
    }
}

void Tree::updatePartialLhFromParent(Node* const node, stack<Node*> &node_stack, const SeqRegions* const parent_upper_regions, const PositionType seq_length)
{
    bool update_blength = false;
    
    // if necessary, update the total probabilities at the mid node.
    if (node->length > 0)
    {
        // update vector of regions at mid-branch point
        updateMidBranchLh(node, parent_upper_regions, node_stack, update_blength);
        
        // if necessary, update the total probability vector.
        if (!update_blength)
        {
            node->computeTotalLhAtNode(aln, model, params->threshold_prob, node == root);
            
            if (!node->total_lh || node->total_lh->size() == 0)
                outError("inside updatePartialLh(), from parent 2: should not have happened since node->length > 0");
        }
    }
    
    // at valid internal node, update upLeft and upRight, and if necessary add children to node_stack.
    if (node->next && !update_blength)
    {
        Node* next_node_1 = node->next;
        Node* next_node_2 = next_node_1->next;
        
        // compute new upper left/right for next_node_1
        SeqRegions* upper_left_right_regions_1 = computeUpperLeftRightRegions(next_node_1, node, parent_upper_regions, node_stack, update_blength);
        SeqRegions* upper_left_right_regions_2 = NULL;
        
        // compute new upper left/right for next_node_1
        if (!update_blength)
            upper_left_right_regions_2 = computeUpperLeftRightRegions(next_node_2, node, parent_upper_regions, node_stack, update_blength);
        
        if (!update_blength)
        {
            // update new partiallh for next_node_1
            updateNewPartialIfDifferent(next_node_1, upper_left_right_regions_2, node_stack, seq_length);
            
            // update new partiallh for next_node_2
            updateNewPartialIfDifferent(next_node_2, upper_left_right_regions_1, node_stack, seq_length);
        }
        
        // delete upper_left_right_regions_1, upper_left_right_regions_2
        if (upper_left_right_regions_1) delete upper_left_right_regions_1;
        if (upper_left_right_regions_2) delete upper_left_right_regions_2;
    }
}

void Tree::updatePartialLhFromChildren(Node* const node, std::stack<Node*> &node_stack, const SeqRegions* const parent_upper_regions, const bool is_non_root, const PositionType seq_length)
{
    bool update_blength = false;
    Node* top_node = NULL;
    Node* other_next_node = NULL;
    Node* next_node = NULL;
    FOR_NEXT(node, next_node)
    {
        if (next_node->is_top)
            top_node = next_node;
        else
            other_next_node = next_node;
    }
    
    ASSERT(top_node && other_next_node);
    
    RealNumType this_node_distance = node->length;
    RealNumType other_next_node_distance = other_next_node->length;
    SeqRegions* this_node_lower_regions = node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
    
    // update lower likelihoods
    SeqRegions* merged_two_lower_regions = NULL;
    SeqRegions* old_lower_regions = NULL;
    other_next_node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob)->mergeTwoLowers(merged_two_lower_regions, other_next_node_distance, *this_node_lower_regions, this_node_distance, aln, model, params->threshold_prob);
    
    if (!merged_two_lower_regions || merged_two_lower_regions->size() == 0)
    {
        handleNullNewRegions(node->neighbor, (this_node_distance <= 0 && other_next_node_distance <= 0), node_stack, update_blength, "Strange: None vector from non-zero distances in updatePartialLh() from child direction.");
    }
    else
    {
        replacePartialLH(old_lower_regions, top_node->partial_lh);
        top_node->partial_lh = merged_two_lower_regions;
        merged_two_lower_regions = NULL;
    }

    // delete merged_two_lower_regions
    if (merged_two_lower_regions) delete merged_two_lower_regions;
    
    // update total likelihood
    if (!update_blength)
    {
        if (top_node->length > 0 || top_node == root)
        {
            SeqRegions* new_total_lh_regions = top_node->computeTotalLhAtNode(aln, model, params->threshold_prob, top_node == root, false);
            
            if (!new_total_lh_regions)
            {
                handleNullNewRegions(top_node, (top_node->length <= 0), node_stack, update_blength, "Strange: None vector from non-zero distances in updatePartialLh() from child direction while doing overall likelihood.");
            }
            else
                replacePartialLH(top_node->total_lh, new_total_lh_regions);
            
            // delete new_total_lh_regions
            if (new_total_lh_regions) delete new_total_lh_regions;
        }
    }
    
    // update total mid-branches likelihood
    if (!update_blength && top_node->length > 0 && is_non_root)
        updateMidBranchLh(top_node, parent_upper_regions, node_stack, update_blength);
    
    if (!update_blength)
    {
        // update likelihoods at parent node
        if (top_node->getPartialLhAtNode(aln, model, params->threshold_prob)->areDiffFrom(*old_lower_regions, seq_length, aln.num_states, &params.value()) && root != top_node)
            node_stack.push(top_node->neighbor);

        // update likelihoods at sibling node
        SeqRegions* new_upper_regions = NULL;
        if (is_non_root)
            parent_upper_regions->mergeUpperLower(new_upper_regions, top_node->length, *this_node_lower_regions, this_node_distance, aln, model, params->threshold_prob);
        else
            new_upper_regions = node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob)->computeTotalLhAtRoot(aln.num_states, model, this_node_distance);
        
        if (!new_upper_regions || new_upper_regions->size() == 0)
        {
            handleNullNewRegions(top_node, (top_node->length <= 0 && this_node_distance <= 0), node_stack, update_blength, "Strange: None vector from non-zero distances in updatePartialLh() from child direction, new_upper_regions.");
        }
        // update partiallh of other_next_node if the new one is different from the current one
        else
            updateNewPartialIfDifferent(other_next_node, new_upper_regions, node_stack, seq_length);
            
        // delete new_upper_regions
       if (new_upper_regions) delete new_upper_regions;
    }
            
    // delete old_lower_regions
    if (old_lower_regions) delete old_lower_regions;
}

void Tree::updatePartialLh(stack<Node*> &node_stack)
{
    (this->*updatePartialLhPointer)(node_stack);
}

template <const StateType num_states>
void Tree::updatePartialLhTemplate(stack<Node*> &node_stack)
{
    const PositionType seq_length = aln.ref_seq.size();
    
    while (!node_stack.empty())
    {
        Node* node = node_stack.top();
        node_stack.pop();
        
        // NHANLT: debug
        /*if (node->next && (node->neighbor->seq_name == "25" || (node->next->neighbor && node->next->neighbor->seq_name == "25") || (node->next->next->neighbor && node->next->next->neighbor->seq_name == "25")))
        //if (node->seq_name == "25")
            cout << "dsdas";*/
        
        node->getTopNode()->outdated = true;
        
        SeqRegions* parent_upper_regions = NULL;
        bool is_non_root = root != node->getTopNode();
        if (is_non_root)
            parent_upper_regions = node->getTopNode()->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
            
        // change in likelihoods is coming from parent node
        if (node->is_top)
        {
            ASSERT(is_non_root);
            updatePartialLhFromParent(node, node_stack, parent_upper_regions, seq_length);
        }
        // otherwise, change in likelihoods is coming from a child.
        else
        {
            updatePartialLhFromChildren(node, node_stack, parent_upper_regions, is_non_root,seq_length);
        }
    }
}

void Tree::examineSamplePlacementMidBranch(Node* &selected_node, RealNumType &best_lh_diff, bool& is_mid_branch, RealNumType& lh_diff_mid_branch, TraversingNode& current_extended_node, const SeqRegions* const sample_regions)
{
    // compute the placement cost
    lh_diff_mid_branch = calculateSamplePlacementCost(current_extended_node.node->mid_branch_lh, sample_regions, default_blength);
    
    // record the best_lh_diff if lh_diff_mid_branch is greater than the best_lh_diff ever
    if (lh_diff_mid_branch > best_lh_diff)
    {
        best_lh_diff = lh_diff_mid_branch;
        selected_node = current_extended_node.node;
        current_extended_node.failure_count = 0;
        is_mid_branch = true;
    }
}

void Tree::examineSamplePlacementAtNode(Node* &selected_node, RealNumType &best_lh_diff, bool& is_mid_branch, RealNumType& lh_diff_at_node, RealNumType& lh_diff_mid_branch, RealNumType &best_up_lh_diff, RealNumType &best_down_lh_diff, Node* &best_child, TraversingNode& current_extended_node, const SeqRegions* const sample_regions)
{
    // compute the placement cost
    lh_diff_at_node = calculateSamplePlacementCost(current_extended_node.node->total_lh, sample_regions, default_blength);
    
    // record the best_lh_diff if lh_diff_at_node is greater than the best_lh_diff ever
    if (lh_diff_at_node > best_lh_diff)
    {
        best_lh_diff = lh_diff_at_node;
        selected_node = current_extended_node.node;
        current_extended_node.failure_count = 0;
        is_mid_branch = false;
        best_up_lh_diff = lh_diff_mid_branch;
    }
    else if (lh_diff_mid_branch >= (best_lh_diff - params->threshold_prob))
    {
        best_up_lh_diff = current_extended_node.likelihood_diff;
        best_down_lh_diff = lh_diff_at_node;
        best_child = current_extended_node.node;
    }
    // placement at current node is considered failed if placement likelihood is not improved by a certain margin compared to best placement so far for the nodes above it.
    else if (lh_diff_at_node < (current_extended_node.likelihood_diff - params->thresh_log_lh_failure))
        ++current_extended_node.failure_count;
}

void Tree::finetuneSamplePlacementAtNode(const Node* const selected_node, RealNumType &best_down_lh_diff, Node* &best_child, const SeqRegions* const sample_regions)
{
    // current node might be part of a polytomy (represented by 0 branch lengths) so we want to explore all the children of the current node to find out if the best placement is actually in any of the branches below the current node.
    Node* neighbor_node;
    stack<Node*> node_stack;
    FOR_NEIGHBOR(selected_node, neighbor_node)
        node_stack.push(neighbor_node);
    
    while (!node_stack.empty())
    {
        Node* node = node_stack.top();
        node_stack.pop();

        if (node->length <= 0)
        {
            FOR_NEIGHBOR(node, neighbor_node)
                node_stack.push(neighbor_node);
        }
        else
        {
            // now try to place on the current branch below the best node, at an height above the mid-branch.
            RealNumType new_blength = node->length * 0.5;
            RealNumType new_best_lh_mid_branch = MIN_NEGATIVE;
            SeqRegions* upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
            SeqRegions* lower_regions = node->getPartialLhAtNode(aln, model, params->threshold_prob);
            SeqRegions* mid_branch_regions = new SeqRegions(node->mid_branch_lh);

            // try to place new sample along the upper half of the current branch
            while (true)
            {
                // compute the placement cost
                RealNumType new_lh_mid_branch = calculateSamplePlacementCost(mid_branch_regions, sample_regions, default_blength);
                
                // record new_best_lh_mid_branch
                if (new_lh_mid_branch > new_best_lh_mid_branch)
                    new_best_lh_mid_branch = new_lh_mid_branch;
                // otherwise, stop trying along the current branch
                else
                    break;
                
                // stop trying if reaching the minimum branch length
                if (new_blength <= min_blength_mid)
                    break;
             
                // try at different position along the current branch
                new_blength *= 0.5;

                // get new mid_branch_regions based on the new_blength
                upper_lr_regions->mergeUpperLower(mid_branch_regions, new_blength, *lower_regions, node->length - new_blength, aln, model, params->threshold_prob);
            }
            
            // delete mid_branch_regions
            delete mid_branch_regions;
            
            //RealNumType new_best_lh_mid_branch = calculateSamplePlacementCost(node->mid_branch_lh, sample_regions, default_blength);
            
            // record new best_down_lh_diff
            if (new_best_lh_mid_branch > best_down_lh_diff)
            {
                best_down_lh_diff = new_best_lh_mid_branch;
                best_child = node;
            }
        }
    }
}

void Tree::seekSamplePlacement(Node* start_node, const string &seq_name, SeqRegions* sample_regions, Node* &selected_node, RealNumType &best_lh_diff, bool &is_mid_branch, RealNumType &best_up_lh_diff, RealNumType &best_down_lh_diff, Node* &best_child)
{
    // init variables
    // output variables
    selected_node = start_node;
    // dummy variables
    RealNumType lh_diff_mid_branch = 0;
    RealNumType lh_diff_at_node = 0;
    // stack of nodes to examine positions
    stack<TraversingNode> extended_node_stack;
    extended_node_stack.push(TraversingNode(start_node, 0, MIN_NEGATIVE));
    
    // recursively examine positions for placing the new sample
    while (!extended_node_stack.empty())
    {
        TraversingNode current_extended_node = extended_node_stack.top();
        Node* current_node = current_extended_node.node;
        extended_node_stack.pop();
        
        // NHANLT: debug
        /*if (current_node->next && ((current_node->next->neighbor && current_node->next->neighbor->seq_name == "25")
                                  || (current_node->next->next->neighbor && current_node->next->next->neighbor->seq_name == "25")))
            cout << "fdsfsd";*/
    
        // if the current node is a leaf AND the new sample/sequence is strictly less informative than the current node
        // -> add the new sequence into the list of minor sequences of the current node + stop seeking the placement
        if ((!current_node->next) && (current_node->partial_lh->compareWithSample(*sample_regions, aln.ref_seq.size(), aln.num_states) == 1))
        {
            current_node->less_info_seqs.push_back(seq_name);
            selected_node = NULL;
            return;
        }
        
        // 1. try first placing as a descendant of the mid-branch point of the branch above the current node
        if (current_node != root && current_node->length > 0)
        {
            examineSamplePlacementMidBranch(selected_node, best_lh_diff, is_mid_branch, lh_diff_mid_branch, current_extended_node, sample_regions);
        }
        // otherwise, don't consider mid-branch point
        else
            lh_diff_mid_branch = MIN_NEGATIVE;

        // 2. try to place as descendant of the current node (this is skipped if the node has top branch length 0 and so is part of a polytomy).
        if (current_node == root || current_node->length > 0)
        {
            examineSamplePlacementAtNode(selected_node, best_lh_diff, is_mid_branch, lh_diff_at_node, lh_diff_mid_branch, best_up_lh_diff, best_down_lh_diff, best_child, current_extended_node, sample_regions);
        }
        else
            lh_diff_at_node = current_extended_node.likelihood_diff;
        
        // keep trying to place at children nodes, unless the number of attempts has reaches the failure limit
        if ((params->strict_stop_seeking_placement_sample
             && current_extended_node.failure_count < params->failure_limit_sample
             && lh_diff_at_node > (best_lh_diff - params->thresh_log_lh_sample))
            || (!params->strict_stop_seeking_placement_sample
                && (current_extended_node.failure_count < params->failure_limit_sample
                    || lh_diff_at_node > (best_lh_diff - params->thresh_log_lh_sample))))
        {
            Node* neighbor_node;
            FOR_NEIGHBOR(current_node, neighbor_node)
            extended_node_stack.push(TraversingNode(neighbor_node, current_extended_node.failure_count, lh_diff_at_node));
        }
    }

    // exploration of the tree is finished, and we are left with the node found so far with the best appending likelihood cost. Now we explore placement just below this node for more fine-grained placement within its descendant branches.
    best_down_lh_diff = MIN_NEGATIVE;
    best_child = NULL;
    
    // if best position so far is the descendant of a node -> explore further at its children
    if (!is_mid_branch)
    {
        finetuneSamplePlacementAtNode(selected_node, best_down_lh_diff, best_child, sample_regions);
    }
}

void Tree::addStartingNodes(const Node* const node, Node* const other_child_node, const RealNumType threshold_prob, SeqRegions* &parent_upper_lr_regions, const RealNumType best_lh_diff, stack<UpdatingNode*> &node_stack)
{
    // node is not the root
    if (node != root)
    {
        parent_upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* other_child_node_regions = other_child_node->getPartialLhAtNode(aln, model, threshold_prob);
       
        // add nodes (sibling and parent of the current node) into node_stack which we will need to traverse to update their regions due to the removal of the sub tree
        RealNumType branch_length = other_child_node->length;
        if (node->length > 0)
            branch_length = branch_length > 0 ? branch_length + node->length : node->length;
        
        //  NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
        node->neighbor->getTopNode()->distance_2_pruning = 1;
        other_child_node->distance_2_pruning = 0;
        node_stack.push(new UpdatingNode(node->neighbor, other_child_node_regions, branch_length, true, best_lh_diff, 0, false));
        node_stack.push(new UpdatingNode(other_child_node, parent_upper_lr_regions, branch_length, true, best_lh_diff, 0, false));
    }
    // node is root
    else
    {
        // there is only one sample outside of the subtree doesn't need to be considered
        if (other_child_node->next)
        {
            // add nodes (children of the sibling of the current node) into node_stack which we will need to traverse to update their regions due to the removal of the sub tree
            Node* grand_child_1 = other_child_node->next->neighbor;
            Node* grand_child_2 = other_child_node->next->next->neighbor;
            
            //  NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
            grand_child_1->distance_2_pruning = 1;
            grand_child_2->distance_2_pruning = 1;
            
            SeqRegions* up_lr_regions_1 = grand_child_2->computeTotalLhAtNode(aln, model, threshold_prob, true, false, grand_child_2->length);
            node_stack.push(new UpdatingNode(grand_child_1, up_lr_regions_1, grand_child_1->length, true, best_lh_diff, 0, true));
            
            SeqRegions* up_lr_regions_2 = grand_child_1->computeTotalLhAtNode(aln, model, threshold_prob, true, false, grand_child_1->length);
            node_stack.push(new UpdatingNode(grand_child_2, up_lr_regions_2, grand_child_2->length, true, best_lh_diff, 0, true));
        }
    }
}

// NOTE: top_node != null <=> case when crawling up from child to parent
// otherwise, top_node == null <=> case we are moving from a parent to a child
bool Tree::examineSubtreePlacementMidBranch(Node* &best_node, RealNumType &best_lh_diff, bool& is_mid_branch, RealNumType& lh_diff_at_node, RealNumType& lh_diff_mid_branch, RealNumType &best_up_lh_diff, RealNumType &best_down_lh_diff, UpdatingNode* const updating_node, const SeqRegions* const subtree_regions, const RealNumType threshold_prob, const RealNumType removed_blength, Node* const top_node, SeqRegions* &bottom_regions)
{
    Node* at_node = top_node ? top_node: updating_node->node;
    SeqRegions* mid_branch_regions = NULL;
    // get or recompute the lh regions at the mid-branch position
    if (updating_node->need_updating)
    {
        // recompute mid_branch_regions in case when crawling up from child to parent
        if (top_node)
        {
            Node* other_child = updating_node->node->getOtherNextNode()->neighbor;
            SeqRegions* other_child_lower_regions = other_child->getPartialLhAtNode(aln, model, threshold_prob);
            other_child_lower_regions->mergeTwoLowers(bottom_regions, other_child->length, *updating_node->incoming_regions, updating_node->branch_length, aln, model, threshold_prob);
                                
            // skip if bottom_regions is null (inconsistent)
            if (!bottom_regions)
            {
                // delete updating_node
                delete updating_node;

                return false; // continue;
            }
           
            // compute new mid-branch regions
            SeqRegions* upper_lr_regions = top_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            RealNumType mid_branch_length = top_node->length * 0.5;
            upper_lr_regions->mergeUpperLower(mid_branch_regions, mid_branch_length, *bottom_regions, mid_branch_length, aln, model, threshold_prob);
        }
        // recompute mid_branch_regions in case we are moving from a parent to a child
        else
        {
            SeqRegions* lower_regions = updating_node->node->getPartialLhAtNode(aln, model, threshold_prob);
            RealNumType mid_branch_length = updating_node->branch_length * 0.5;
            updating_node->incoming_regions->mergeUpperLower(mid_branch_regions, mid_branch_length, *lower_regions, mid_branch_length, aln, model, threshold_prob);
        }
    }
    else
        mid_branch_regions = at_node->mid_branch_lh;
    
    // skip if mid_branch_regions is null (branch length == 0)
    if (!mid_branch_regions)
    {
        // delete bottom_regions if it's existed
        if (bottom_regions) delete bottom_regions;
                            
        // delete updating_node
        delete updating_node;
        
        return false; // continue;
    }
    
    // compute the placement cost
    // if (search_subtree_placement)
    lh_diff_mid_branch = calculateSubTreePlacementCost(mid_branch_regions, subtree_regions, removed_blength);
    /*else
        lh_diff_mid_branch = calculateSamplePlacementCost( mid_branch_regions, subtree_regions, removed_blength);*/
    
    if (top_node && best_node == top_node) // only update in case when crawling up from child to parent
        best_up_lh_diff = lh_diff_mid_branch;
    
    // if this position is better than the best position found so far -> record it
    if (lh_diff_mid_branch > best_lh_diff)
    {
        best_node = at_node;
        best_lh_diff = lh_diff_mid_branch;
        is_mid_branch = true;
        updating_node->failure_count = 0;
        if (top_node) best_down_lh_diff = lh_diff_at_node; // only update in case when crawling up from child to parent
    }
    else if (top_node && lh_diff_at_node >= (best_lh_diff - threshold_prob))
        best_up_lh_diff = lh_diff_mid_branch;
        
    // delete mid_branch_regions
    if (updating_node->need_updating) delete mid_branch_regions;
    
    // no error
    return true;
}

// NOTE: top_node != null <=> case when crawling up from child to parent
// otherwise, top_node == null <=> case we are moving from a parent to a child
bool Tree::examineSubTreePlacementAtNode(Node* &best_node, RealNumType &best_lh_diff, bool& is_mid_branch, RealNumType& lh_diff_at_node, RealNumType& lh_diff_mid_branch, RealNumType &best_up_lh_diff, RealNumType &best_down_lh_diff, UpdatingNode* const updating_node, const SeqRegions* const subtree_regions, const RealNumType threshold_prob, const RealNumType removed_blength, Node* const top_node)
{
    const PositionType seq_length = aln.ref_seq.size();
    const StateType num_states = aln.num_states;
    
    Node* at_node = top_node ? top_node: updating_node->node;
    SeqRegions* at_node_regions = NULL;
    bool delete_at_node_regions = false;
    if (updating_node->need_updating)
    {
        delete_at_node_regions = true;
        // get or recompute the lh regions at the current node position
        
        SeqRegions* updating_node_partial = updating_node->node->getPartialLhAtNode(aln, model, threshold_prob);
        if (top_node)
        {
            updating_node_partial->mergeUpperLower(at_node_regions, -1, *updating_node->incoming_regions, updating_node->branch_length, aln, model, threshold_prob);
        }
        else
        {
            updating_node->incoming_regions->mergeUpperLower(at_node_regions, updating_node->branch_length, *updating_node_partial, -1, aln, model, threshold_prob);
        }
        
        // skip if at_node_regions is null (branch length == 0)
        if (!at_node_regions)
        {
            // delete updating_node
            delete updating_node;
            
            // continue;
            return false;
        }
        
        // stop updating if the difference between the new and old regions is insignificant
        if  (!at_node_regions->areDiffFrom(*at_node->total_lh, seq_length, num_states, &params.value()))
            updating_node->need_updating = false;
    }
    else
        at_node_regions = at_node->total_lh;
    
    //if (search_subtree_placement)
    lh_diff_at_node = calculateSubTreePlacementCost(at_node_regions, subtree_regions, removed_blength);
    /*else
        lh_diff_at_node = calculateSamplePlacementCost(at_node_regions, subtree_regions, removed_blength);*/
    
    // if this position is better than the best position found so far -> record it
    if (lh_diff_at_node > best_lh_diff)
    {
        best_node = at_node;
        best_lh_diff = lh_diff_at_node;
        is_mid_branch = false;
        updating_node->failure_count = 0;
        if (!top_node) best_up_lh_diff = lh_diff_mid_branch; // only update in case we are moving from a parent to a child
    }
    else if (!top_node && lh_diff_mid_branch >= (best_lh_diff - threshold_prob)) // only update in case we are moving from a parent to a child
    {
        best_up_lh_diff = updating_node->likelihood_diff;
        best_down_lh_diff = lh_diff_at_node;
    }
    // placement at current node is considered failed if placement likelihood is not improved by a certain margin compared to best placement so far for the nodes above it.
    else if (lh_diff_at_node < (updating_node->likelihood_diff - params->thresh_log_lh_failure))
        ++updating_node->failure_count;
        
    // delete at_node_regions
    if (delete_at_node_regions) delete at_node_regions;
    
    // no error
    return true;
}

bool keepTraversing(const RealNumType& best_lh_diff, const RealNumType& lh_diff_at_node, const bool& strict_stop_seeking_placement_subtree, UpdatingNode* const updating_node, const int& failure_limit_subtree, const RealNumType& thresh_log_lh_subtree, const bool able2traverse = true)
{
    /*if (search_subtree_placement)
    {*/
    if (strict_stop_seeking_placement_subtree)
    {
        if (updating_node->failure_count <= failure_limit_subtree && lh_diff_at_node > (best_lh_diff - thresh_log_lh_subtree) && able2traverse)
            return true;
    }
    else
    {
        if ((updating_node->failure_count <= failure_limit_subtree || lh_diff_at_node > (best_lh_diff - thresh_log_lh_subtree))
            && able2traverse)
            return true;
    }
    /*}
    else
    {
        if (params->strict_stop_seeking_placement_sample)
        {
            if (updating_node->failure_count <= params->failure_limit_sample && lh_diff_at_node > (best_lh_diff - params->thresh_log_lh_sample)
                && updating_node->node->next)
                    return true;
        }
        else
        {
            if ((updating_node->failure_count <= params->failure_limit_sample || lh_diff_at_node > (best_lh_diff - params->thresh_log_lh_sample))
                && updating_node->node->next)
                    return true;
        }
    }*/
    
    // default
    return false;
}

void Tree::addChildSeekSubtreePlacement(Node* const child_1, Node* const child_2, const RealNumType& lh_diff_at_node, UpdatingNode* const updating_node, stack<UpdatingNode*>& node_stack, const RealNumType threshold_prob)
{
    // add child_1 to node_stack
    SeqRegions* upper_lr_regions = NULL;
    SeqRegions* lower_regions = child_2->getPartialLhAtNode(aln, model, threshold_prob);
    // get or recompute the upper left/right regions of the children node
    if (updating_node->need_updating)
        updating_node->incoming_regions->mergeUpperLower(upper_lr_regions, updating_node->branch_length, *lower_regions, child_2->length, aln, model, threshold_prob);
    else
    {
        if (child_1->neighbor->partial_lh)
            upper_lr_regions = child_1->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    }
    // traverse to this child's subtree
    if (upper_lr_regions)
        node_stack.push(new UpdatingNode(child_1, upper_lr_regions, child_1->length, updating_node->need_updating, lh_diff_at_node, updating_node->failure_count, updating_node->need_updating));
}

bool Tree::addNeighborsSeekSubtreePlacement(Node* const top_node, Node* const other_child, SeqRegions* &parent_upper_lr_regions, SeqRegions* &bottom_regions, const RealNumType& lh_diff_at_node, UpdatingNode* const updating_node, std::stack<UpdatingNode*>& node_stack, const RealNumType threshold_prob)
{
    // keep crawling up into parent and sibling node
    // case the node is not the root
    if (top_node != root)
    {
        // first pass the crawling down the other child (sibling)
        parent_upper_lr_regions = top_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        
        SeqRegions* upper_lr_regions = NULL;
        // get or recompute the upper left/right regions of the sibling node
        if (updating_node->need_updating)
            parent_upper_lr_regions->mergeUpperLower(upper_lr_regions, top_node->length, *updating_node->incoming_regions, updating_node->branch_length, aln, model, threshold_prob);
        else
        {
            if (updating_node->node->partial_lh)
                upper_lr_regions = updating_node->node->getPartialLhAtNode(aln, model, threshold_prob);
        }

        // add sibling node to node_stack for traversing later; skip if upper_lr_regions is null (inconsistent)
        if (!upper_lr_regions)
        {
            // delete bottom_regions if it's existed
            if (bottom_regions) delete bottom_regions;
            
            // delete updating_node
            delete updating_node;
            
            return false; // continue;
        }
        else
        {
            node_stack.push(new UpdatingNode(other_child, upper_lr_regions, other_child->length, updating_node->need_updating, lh_diff_at_node, updating_node->failure_count, updating_node->need_updating));
        }
        
        // now pass the crawling up to the parent node
        // get or recompute the bottom regions (comming from 2 children) of the parent node
        if (updating_node->need_updating)
        {
            if (!bottom_regions)
            {
                SeqRegions* other_child_lower_regions = other_child->getPartialLhAtNode(aln, model, threshold_prob);
                other_child_lower_regions->mergeTwoLowers(bottom_regions, other_child->length, *updating_node->incoming_regions, updating_node->branch_length, aln, model, threshold_prob);
                
                // skip if bottom_regions is null (inconsistent)
                if (!bottom_regions)
                {
                    // delete updating_node
                    delete updating_node;
                    
                    return false; // continue;
                }
            }
        }
        else
        {
            if (bottom_regions) delete bottom_regions;
            bottom_regions = top_node->getPartialLhAtNode(aln, model, threshold_prob);
        }
        
        // add the parent node to node_stack for traversing later
        // NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
        top_node->neighbor->getTopNode()->distance_2_pruning = top_node->distance_2_pruning + 1;
        node_stack.push(new UpdatingNode(top_node->neighbor, bottom_regions, top_node->length, updating_node->need_updating, lh_diff_at_node, updating_node->failure_count, updating_node->need_updating));
    }
    // now consider case of root node -> only need to care about the sibling node
    else
    {
        SeqRegions* upper_lr_regions = NULL;
        
        // get or recompute the upper left/right regions of the sibling node
        if (updating_node->need_updating)
            upper_lr_regions = updating_node->incoming_regions->computeTotalLhAtRoot(aln.num_states, model, updating_node->branch_length);
        else
            upper_lr_regions = updating_node->node->getPartialLhAtNode(aln, model, threshold_prob);
        
        // add the sibling node to node_stack for traversing later
        node_stack.push(new UpdatingNode(other_child, upper_lr_regions, other_child->length, updating_node->need_updating, lh_diff_at_node, updating_node->failure_count, updating_node->need_updating));
        
        // delete bottom_regions
        if (bottom_regions) delete bottom_regions;
    }
    
    return true;
}

void Tree::seekSubTreePlacement(Node* &best_node, RealNumType &best_lh_diff, bool &is_mid_branch, RealNumType &best_up_lh_diff, RealNumType &best_down_lh_diff, Node* &best_child, bool short_range_search, Node* child_node, RealNumType &removed_blength, bool search_subtree_placement, SeqRegions* sample_regions)
{
    // init variables
    Node* node = child_node->neighbor->getTopNode();
    Node* other_child_node = child_node->neighbor->getOtherNextNode()->neighbor;
    best_node = node;
    SeqRegions* subtree_regions = NULL;
    // stack of nodes to examine positions
    stack<UpdatingNode*> node_stack;
    // dummy variables
    RealNumType threshold_prob = params->threshold_prob;
    RealNumType lh_diff_mid_branch = 0;
    RealNumType lh_diff_at_node = 0;
    SeqRegions* parent_upper_lr_regions = NULL;
    PositionType seq_length = aln.ref_seq.size();
    StateType num_states = aln.num_states;
    
    // get/init approximation params
    bool strict_stop_seeking_placement_subtree = params->strict_stop_seeking_placement_subtree;
    int failure_limit_subtree = params->failure_limit_subtree;
    RealNumType thresh_log_lh_subtree = params->thresh_log_lh_subtree;
    
    // for short range topology search
    if (short_range_search)
    {
        strict_stop_seeking_placement_subtree = params->strict_stop_seeking_placement_subtree_short_search;
        failure_limit_subtree = params->failure_limit_subtree_short_search;
        thresh_log_lh_subtree = params->thresh_log_lh_subtree_short_search;
    }

    // search a placement for a subtree
    /*if (search_subtree_placement)
    {*/
    // get the lower regions of the child node
    subtree_regions = child_node->getPartialLhAtNode(aln, model, threshold_prob);
    
    // add starting nodes to start seek placement for the subtree
    addStartingNodes(node, other_child_node, threshold_prob, parent_upper_lr_regions, best_lh_diff, node_stack);
    
    /*}
     // search a placement for a new sample
    else
    {
        // get the regions of the input sample
        subtree_regions = sample_regions;
        RealNumType down_lh = is_mid_branch ? best_down_lh_diff : best_lh_diff;
        
        // node is not the root
        if (node != root)
        {
            SeqRegions* lower_regions = new SeqRegions(node->getPartialLhAtNode(aln, model, threshold_prob), num_states);
            parent_upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            
            // add the parent node of the current node into node_stack for traversing to seek the placement for the new sample
            node_stack.push(new UpdatingNode(node->neighbor, lower_regions, node->length, false, down_lh, 0));
        }
        
        // add the children nodes of the current node into node_stack for traversing to seek the placement for the new sample
        Node* neighbor_node;
        FOR_NEIGHBOR(node, neighbor_node)
        {
            SeqRegions* upper_lr_regions = new SeqRegions(neighbor_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob), num_states);
            node_stack.push(new UpdatingNode(neighbor_node, upper_lr_regions, neighbor_node->length, false, down_lh, 0));
        }
     }*/
    
    // examine each node in the node stack to seek the "best" placement
    while (!node_stack.empty())
    {
        // extract updating_node from stack
        UpdatingNode* updating_node = node_stack.top();
        node_stack.pop();
        
        // consider the case we are moving from a parent to a child
        if (updating_node->node->is_top)
        {
            if (updating_node->node->length > 0)
            {
                //  try to append mid-branch
                // avoid adding to the old position where the subtree was just been removed from
                if (updating_node->node != root && updating_node->node->neighbor->getTopNode() != node)
                {
                    SeqRegions* bottom_regions = NULL;
                    if (!examineSubtreePlacementMidBranch(best_node, best_lh_diff, is_mid_branch, lh_diff_at_node, lh_diff_mid_branch, best_up_lh_diff, best_down_lh_diff, updating_node, subtree_regions, threshold_prob, removed_blength, NULL, bottom_regions)) continue;
                }
                // set the placement cost at the mid-branch position the most negative value if branch length is zero -> we can't place the subtree on that branch
                else
                    lh_diff_mid_branch = MIN_NEGATIVE;
                    
                // now try appending exactly at node
                if(!examineSubTreePlacementAtNode(best_node, best_lh_diff, is_mid_branch, lh_diff_at_node, lh_diff_mid_branch, best_up_lh_diff, best_down_lh_diff, updating_node, subtree_regions, threshold_prob, removed_blength, NULL)) continue;
            }
            // set the placement cost at the current node position at the most negative value if branch length is zero -> we can't place the subtree on that branch
            else
                lh_diff_at_node = updating_node->likelihood_diff;
            
            // keep crawling down into children nodes unless the stop criteria for the traversal are satisfied.
            // check the stop criteria
            // keep traversing further down to the children
            if (keepTraversing(best_lh_diff, lh_diff_at_node, strict_stop_seeking_placement_subtree, updating_node, failure_limit_subtree, thresh_log_lh_subtree, updating_node->node->next))
            {
                Node* child_1 = updating_node->node->getOtherNextNode()->neighbor;
                Node* child_2 = child_1->neighbor->getOtherNextNode()->neighbor;
                //  NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
                child_1->distance_2_pruning = updating_node->node->distance_2_pruning + 1;
                child_2->distance_2_pruning = updating_node->node->distance_2_pruning + 1;
                
                // add child_1 to node_stack
                addChildSeekSubtreePlacement(child_1, child_2, lh_diff_at_node, updating_node, node_stack, threshold_prob);
                
                // add child_2 to node_stack
                addChildSeekSubtreePlacement(child_2, child_1, lh_diff_at_node, updating_node, node_stack, threshold_prob);
            }
        }
        // case when crawling up from child to parent
        else
        {
            Node* top_node = updating_node->node->getTopNode();
            
            // append directly at the node
            if (top_node->length > 0 || top_node == root)
            {
                if (!examineSubTreePlacementAtNode(best_node, best_lh_diff, is_mid_branch, lh_diff_at_node, lh_diff_mid_branch, best_up_lh_diff, best_down_lh_diff, updating_node, subtree_regions, threshold_prob, removed_blength, top_node)) continue;
            }
            // if placement cost at new position gets worse -> restore to the old one
            else
                lh_diff_at_node = updating_node->likelihood_diff;

            // try appending mid-branch
            Node* other_child = updating_node->node->getOtherNextNode()->neighbor;
            SeqRegions* bottom_regions = NULL;
            if (top_node->length > 0 && top_node != root)
            {
                if (!examineSubtreePlacementMidBranch(best_node, best_lh_diff, is_mid_branch, lh_diff_at_node, lh_diff_mid_branch, best_up_lh_diff, best_down_lh_diff, updating_node, subtree_regions, threshold_prob, removed_blength, top_node, bottom_regions)) continue;
            }
            // set the placement cost at the mid-branch position at the most negative value if branch length is zero -> we can't place the subtree on that branch
            // NHANLT: we actually don't need to do that since lh_diff_mid_branch will never be read
            // else
                // lh_diff_mid_branch = MIN_NEGATIVE;
            
            // check stop rule of the traversal process
            // keep traversing upwards
            if (keepTraversing(best_lh_diff, lh_diff_at_node, strict_stop_seeking_placement_subtree, updating_node, failure_limit_subtree, thresh_log_lh_subtree))
            {
                // NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
                other_child->distance_2_pruning = top_node->distance_2_pruning + 1;
                if(!addNeighborsSeekSubtreePlacement(top_node, other_child, parent_upper_lr_regions, bottom_regions, lh_diff_at_node, updating_node, node_stack, threshold_prob)) continue;
            }
            else
            {
                // delete bottom_regions if it's existed
                if (bottom_regions) delete bottom_regions;
            }
        }
        
        // delete updating_node
        delete updating_node;
    }
    
    // exploration of the tree is finished, and we are left with the node found so far with the best appending likelihood cost.
    // Now we explore placement just below this node for more fine-grained placement within its descendant branches.
    /*if (!search_subtree_placement)
    {
        best_down_lh_diff = MIN_NEGATIVE;
        best_child = NULL;
        
        if (is_mid_branch)
        {
            // go upward until we reach the parent node of a polytomy
            Node* parent_node = best_node->neighbor->getTopNode();
            while (parent_node->length <= 0 && parent_node != root)
                parent_node = parent_node->neighbor->getTopNode();
            
            best_up_lh_diff = calculateSamplePlacementCost(parent_node->total_lh, subtree_regions, removed_blength);
            child_node = best_node;
        }
        else
        {
            // current node might be part of a polytomy (represented by 0 branch lengths) so we want to explore all the children of the current node to find out if the best placement is actually in any of the branches below the current node.
            Node* neighbor_node;
            stack<Node*> new_node_stack;
            FOR_NEIGHBOR(best_node, neighbor_node)
                new_node_stack.push(neighbor_node);
            
            while (!new_node_stack.empty())
            {
                Node* node = new_node_stack.top();
                new_node_stack.pop();
                
                if (node->length <= 0)
                {
                    FOR_NEIGHBOR(node, neighbor_node)
                        new_node_stack.push(neighbor_node);
                }
                // now try to place on the current branch below the best node, at an height above the mid-branch.
                else
                {
                    // now try to place on the current branch below the best node, at an height above the mid-branch.
                    RealNumType new_blength = node->length * 0.5;
                    RealNumType new_best_lh_mid_branch = MIN_NEGATIVE;
                    SeqRegions* upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
                    SeqRegions* lower_regions = node->getPartialLhAtNode(aln, model, params->threshold_prob);
                    SeqRegions* mid_branch_regions = new SeqRegions(node->mid_branch_lh, aln.num_states);

                    // try to place new sample along the upper half of the current branch
                    while (true)
                    {
                        // compute the placement cost
                        RealNumType new_lh_mid_branch = calculateSamplePlacementCost(mid_branch_regions, subtree_regions, removed_blength);
                        
                        // record new_best_lh_mid_branch
                        if (new_lh_mid_branch > new_best_lh_mid_branch)
                            new_best_lh_mid_branch = new_lh_mid_branch;
                        // otherwise, stop trying along the current branch
                        else
                            break;
                        
                        // stop trying if reaching the minimum branch length
                        if (new_blength <= min_blength_mid)
                            break;
     
                         // try at different position along the current branch
                         new_blength *= 0.5;
                     
                        // get new mid_branch_regions based on the new_blength
                        upper_lr_regions->mergeUpperLower(mid_branch_regions, new_blength, lower_regions, node->length - new_blength, aln, model, params->threshold_prob);
                    }
                    
                    //RealNumType new_best_lh_mid_branch = calculateSamplePlacementCost(node->mid_branch_lh, sample_regions, default_blength);
                    
                    // record new best_down_lh_diff
                    if (new_best_lh_mid_branch > best_down_lh_diff)
                    {
                        best_down_lh_diff = new_best_lh_mid_branch;
                        best_child = node;
                    }
                }
            }
        }
    }*/
}

void Tree::applySPR(Node* const subtree, Node* const best_node, const bool is_mid_branch, const RealNumType branch_length, const RealNumType best_lh_diff)
{
    // remove subtree from the tree
    Node* parent_subtree = subtree->neighbor->getTopNode();
    Node* sibling_subtree = subtree->neighbor->getOtherNextNode()->neighbor;
    RealNumType threshold_prob = params->threshold_prob;
    StateType num_states = aln.num_states;
    
    // connect grandparent to sibling
    if (parent_subtree != root)
        parent_subtree->neighbor->neighbor = sibling_subtree;
    sibling_subtree->neighbor = parent_subtree->neighbor;
    
    // update the length of the branch connecting grandparent to sibling
    if (sibling_subtree->length > 0)
    {
        if (parent_subtree->length > 0)
            sibling_subtree->length += parent_subtree->length;
    }
    else
        sibling_subtree->length = parent_subtree->length;
    
    // update likelihood lists after subtree removal
    // case when the sibling_subtree becomes the new root
    if (!sibling_subtree->neighbor)
    {
        // update root
        root = sibling_subtree;
        
        // delete mid_branch_lh
        if (root->mid_branch_lh)
        {
            delete sibling_subtree->mid_branch_lh;
            sibling_subtree->mid_branch_lh = NULL;
        }
        
        // reset branch length (to 0) if sibling_subtree is root
        sibling_subtree->length = 0;

        // recompute the total lh regions at sibling
        sibling_subtree->computeTotalLhAtNode(aln, model, threshold_prob, true);
        
        // traverse downwards (to childrens of the sibling) to update their lh regions
        if (sibling_subtree->next)
        {
            // update upper left/right regions
            Node* next_node_1 = sibling_subtree->next;
            Node* next_node_2 = next_node_1->next;
            
            if (next_node_1->partial_lh) delete next_node_1->partial_lh;
            SeqRegions* lower_reions = next_node_2->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            next_node_1->partial_lh = lower_reions->computeTotalLhAtRoot(num_states, model, next_node_2->length);
            
            if (next_node_2->partial_lh) delete next_node_2->partial_lh;
            lower_reions = next_node_1->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            next_node_2->partial_lh = lower_reions->computeTotalLhAtRoot(num_states, model, next_node_1->length);
            
            // add children to node_stack for further traversing and updating likelihood regions
            stack<Node*> node_stack;
            node_stack.push(next_node_1->neighbor);
            node_stack.push(next_node_2->neighbor);
            updatePartialLh(node_stack);
        }
    }
    // case when the sibling_subtree is non-root node
    else
    {
        // update branch length from the grandparent side
        sibling_subtree->neighbor->length = sibling_subtree->length;
        
        // traverse to parent and sibling node to update their likelihood regions due to subtree remova
        stack<Node*> node_stack;
        node_stack.push(sibling_subtree);
        node_stack.push(sibling_subtree->neighbor);
        updatePartialLh(node_stack);
    }
    
    // replace the node and re-update the vector lists
    SeqRegions* subtree_lower_regions = subtree->getPartialLhAtNode(aln, model, threshold_prob);
    // try to place the new sample as a descendant of a mid-branch point
    if (is_mid_branch && best_node != root)
        placeSubTreeMidBranch(best_node, subtree, subtree_lower_regions, branch_length, best_lh_diff);
    // otherwise, best lk so far is for appending directly to existing node
    else
        placeSubTreeAtNode(best_node, subtree, subtree_lower_regions, branch_length, best_lh_diff);
}

void Tree::updateRegionsPlaceSubTree(Node* const subtree, Node* const next_node_1, Node* const sibling_node, Node* const new_internal_node, SeqRegions* &best_child_regions, const SeqRegions* const subtree_regions, const  SeqRegions* const upper_left_right_regions, const SeqRegions* const lower_regions, RealNumType &best_blength)
{
    // update next_node_1->partial_lh
    replacePartialLH(next_node_1->partial_lh, best_child_regions);

    // update new_internal_node->partial_lh
    sibling_node->getPartialLhAtNode(aln, model, params->threshold_prob)->mergeTwoLowers(new_internal_node->partial_lh, sibling_node->length, *subtree_regions, best_blength, aln, model, params->threshold_prob);
}

void Tree::updateRegionsPlaceSubTreeAbove(Node* const subtree, Node* const next_node_1, Node* const sibling_node, Node* const new_internal_node, SeqRegions* &best_child_regions, const SeqRegions* const subtree_regions, const  SeqRegions* const upper_left_right_regions, const SeqRegions* const lower_regions, RealNumType &best_length)
{
    sibling_node->getPartialLhAtNode(aln, model, params->threshold_prob)->mergeTwoLowers(new_internal_node->partial_lh, sibling_node->length, *subtree_regions, best_length, aln, model, params->threshold_prob);
    if (!new_internal_node->partial_lh)
    {
        outWarning("Problem, non lower likelihood while placing subtree -> set best branch length to min length");
        best_length = min_blength;
        subtree->length = best_length;
        subtree->neighbor->length = best_length;
        lower_regions->mergeTwoLowers(new_internal_node->partial_lh, sibling_node->length, *subtree_regions, best_length, aln, model, params->threshold_prob);
    }
    upper_left_right_regions->mergeUpperLower(next_node_1->partial_lh, new_internal_node->length, *lower_regions, sibling_node->length, aln, model, params->threshold_prob);
}

template<void (Tree::*updateRegionsSubTree)(Node* const, Node* const, Node* const, Node* const, SeqRegions* &, const SeqRegions* const, const  SeqRegions* const, const SeqRegions* const, RealNumType&)>
void Tree::connectSubTree2Branch(const SeqRegions* const subtree_regions, const SeqRegions* const lower_regions, Node* const subtree, Node* const sibling_node, const RealNumType top_distance, const RealNumType down_distance, RealNumType &best_blength, SeqRegions* &best_child_regions, const SeqRegions* const upper_left_right_regions)
{
    //  NHANLT - DELETE: extremely uneffecient because opening file multiple times but just for testing
    // log the depth of pruning and regrafting branches
    // open the tree file
    string output_file(params->diff_path);
    ofstream out = ofstream(output_file + ".statistics.txt", std::ios_base::app);
    // write depths: original pruning branch; original regrafting branch; actual pruning branch; actual regrafting branch; distance (regarding the number of branches) between the pruning and the regrafting branches
    out << subtree->depth << "\t" << sibling_node->depth << "\t" << getNewDepth(subtree) << "\t" << getNewDepth(sibling_node) << "\t" << sibling_node->distance_2_pruning << endl;
    // close the output file
    out.close();
    
    const RealNumType threshold_prob = params->threshold_prob;
    
    // re-use internal nodes
    Node* next_node_1 = subtree->neighbor;
    Node* new_internal_node = next_node_1->getTopNode();
    Node* next_node_2 = next_node_1->getOtherNextNode();
    
    // NHANLT NOTES: UNNECESSARY
    // re-order next circle (not neccessary, just to make it consistent with Python code)
    new_internal_node->next = next_node_2;
    next_node_2->next = next_node_1;
    next_node_1->next = new_internal_node;
    
    // connect new_internal_node to the parent of the selected node
    new_internal_node->outdated = true;
    sibling_node->neighbor->neighbor = new_internal_node;
    new_internal_node->neighbor = sibling_node->neighbor;
    new_internal_node->length = top_distance;
    new_internal_node->neighbor->length = top_distance;
    
    // connect the selected_node to new_internal_node (via next_node_2)
    sibling_node->neighbor = next_node_2;
    next_node_2->neighbor = sibling_node;
    sibling_node->length = down_distance;
    sibling_node->neighbor->length = down_distance;
    
    // subtree already connected to new_internal_node (via next_node_1)
    subtree->length = best_blength;
    subtree->neighbor->length = best_blength;
            
    // update all likelihood regions
    (this->*updateRegionsSubTree)(subtree, next_node_1, sibling_node, new_internal_node, best_child_regions, subtree_regions, upper_left_right_regions, lower_regions, best_blength);
    
    upper_left_right_regions->mergeUpperLower(next_node_2->partial_lh, new_internal_node->length, *subtree_regions, best_blength, aln, model, threshold_prob);
    RealNumType mid_branch_length = new_internal_node->length * 0.5;
    upper_left_right_regions->mergeUpperLower(new_internal_node->mid_branch_lh, mid_branch_length, *new_internal_node->partial_lh, mid_branch_length, aln, model, threshold_prob);
    new_internal_node->computeTotalLhAtNode(aln, model, threshold_prob, new_internal_node == root);
    
    if (!new_internal_node->total_lh || new_internal_node->total_lh->size() == 0)
        outError("Problem, None vector when re-placing sample, placing subtree at mid-branch point");
    
    /*if distTop>=2*min_blengthForMidNode:
     createFurtherMidNodes(newInternalNode,upper_left_right_regions)*/

    // iteratively traverse the tree to update partials from the current node
    stack<Node*> node_stack;
    node_stack.push(sibling_node);
    node_stack.push(subtree);
    node_stack.push(new_internal_node->neighbor);
    updatePartialLh(node_stack);
}

void Tree::placeSubTreeMidBranch(Node* const selected_node, Node* const subtree, const SeqRegions* const subtree_regions, const RealNumType new_branch_length, const RealNumType new_lh)
{
    SeqRegions* best_child_regions = NULL;
    RealNumType threshold_prob = params->threshold_prob;
    SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    // RealNumType best_split = 0.5;
    RealNumType best_blength_split = selected_node->length * 0.5;
    RealNumType best_split_lh = new_lh;
    // RealNumType new_split = 0.25;
    best_child_regions = new SeqRegions(selected_node->mid_branch_lh);
    SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);

    // try different positions on the existing branch
    bool found_new_split = tryShorterBranch<&Tree::calculateSubTreePlacementCost>(selected_node->length, best_child_regions, subtree_regions, upper_left_right_regions, lower_regions, best_split_lh, best_blength_split, new_branch_length, true);
    
    if (!found_new_split)
    {
        found_new_split = tryShorterBranch<&Tree::calculateSubTreePlacementCost>(selected_node->length, best_child_regions, subtree_regions, upper_left_right_regions, lower_regions, best_split_lh, best_blength_split, new_branch_length, false);
        
        if (found_new_split)
            best_blength_split = selected_node->length - best_blength_split;
    }
    
    // now try different lengths for the new branch
    RealNumType best_blength = new_branch_length;
    estimateLengthNewBranch<&Tree::calculateSubTreePlacementCost>(best_split_lh, best_child_regions, subtree_regions, best_blength, max_blength, double_min_blength, (new_branch_length <= 0));
    
    // attach subtree to the branch above the selected node
    connectSubTree2Branch<&Tree::updateRegionsPlaceSubTree>(subtree_regions, NULL, subtree, selected_node, best_blength_split, selected_node->length - best_blength_split, best_blength, best_child_regions, upper_left_right_regions);
    
    // delete best_child_regions
    if (best_child_regions)
        delete best_child_regions;
}

void Tree::connectSubTree2Root(Node* const subtree, const SeqRegions* const subtree_regions, const SeqRegions* const lower_regions, Node* const sibling_node, const RealNumType best_root_blength, const RealNumType best_length2, SeqRegions* &best_parent_regions)
{
    //  NHANLT - DELETE: extremely uneffecient because opening file multiple times but just for testing
    // log the depth of pruning and regrafting branches
    // open the tree file
    string output_file(params->diff_path);
    ofstream out = ofstream(output_file + ".statistics.txt", std::ios_base::app);
    // write depths: original pruning branch; original regrafting branch; actual pruning branch; actual regrafting branch; distance (regarding the number of branches) between the pruning and the regrafting branches
    out << subtree->depth << "\t" << sibling_node->depth << "\t" << getNewDepth(subtree) << "\t" << getNewDepth(sibling_node) << "\t" << sibling_node->distance_2_pruning << endl;
    // close the output file
    out.close();
    
    // re-use internal nodes
    Node* next_node_1 = subtree->neighbor;
    Node* new_root = next_node_1->getTopNode();
    Node* next_node_2 = next_node_1->getOtherNextNode();
    
    // NHANLT NOTES: UNNECESSARY
    // re-order next circle (not neccessary, just to make it consistent with Python code)
    new_root->next = next_node_2;
    next_node_2->next = next_node_1;
    next_node_1->next = new_root;
    
    // connect new_internal_node to the parent of the selected node
    new_root->outdated = true;
    new_root->neighbor = sibling_node->neighbor; // actually NULL since selected_node is root
    new_root->length = 0;
    
    // connect the selected_node to new_internal_node (via next_node_2)
    sibling_node->neighbor = next_node_2;
    next_node_2->neighbor = sibling_node;
    sibling_node->length = best_root_blength;
    sibling_node->neighbor->length = best_root_blength;
    if (best_root_blength <= 0)
    {
        delete sibling_node->total_lh;
        sibling_node->total_lh = NULL;
        
        if (sibling_node->mid_branch_lh) delete sibling_node->mid_branch_lh;
        sibling_node->mid_branch_lh = NULL;
        //selected_node.furtherMidNodes=None
    }
    
    // subtree already connected to new_internal_node (via next_node_1)
    subtree->length = best_length2;
    subtree->neighbor->length = best_length2;
            
    // update all likelihood regions
    replacePartialLH(new_root->partial_lh, best_parent_regions);
    if (new_root->mid_branch_lh) delete new_root->mid_branch_lh;
    new_root->mid_branch_lh = NULL;
    new_root->computeTotalLhAtNode(aln, model, params->threshold_prob, true);
    if (next_node_1->partial_lh) delete next_node_1->partial_lh;
    next_node_1->partial_lh = lower_regions->computeTotalLhAtRoot(aln.num_states, model, best_root_blength);
    if (next_node_2->partial_lh) delete next_node_2->partial_lh;
    next_node_2->partial_lh = subtree_regions->computeTotalLhAtRoot(aln.num_states, model, best_length2);
    
    if (!new_root->total_lh || new_root->total_lh->size() == 0)
        outWarning("Problem, None vector when re-placing sample, position root");
    
    // update tree->root;
    root = new_root;
    
    // iteratively traverse the tree to update partials from the current node
    stack<Node*> node_stack;
    node_stack.push(sibling_node);
    node_stack.push(subtree);
    updatePartialLh(node_stack);
}

void Tree::handlePolytomyPlaceSubTree(Node* const selected_node, const SeqRegions* const subtree_regions, const RealNumType new_branch_length, RealNumType &best_down_lh_diff, Node* &best_child, RealNumType &best_child_blength_split, SeqRegions* &best_child_regions)
{
    // current node might be part of a polytomy (represented by 0 branch lengths) so we want to explore all the children of the current node to find out if the best placement is actually in any of the branches below the current node.
    stack<Node*> new_node_stack;
    const RealNumType threshold_prob = params->threshold_prob;
    Node* neighbor_node;
    FOR_NEIGHBOR(selected_node, neighbor_node)
    {
        // NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
        neighbor_node->distance_2_pruning = selected_node->distance_2_pruning + 1;
        new_node_stack.push(neighbor_node);
    }
    
    while (!new_node_stack.empty())
    {
        Node* node = new_node_stack.top();
        new_node_stack.pop();

        // add all nodes in polytomy
        if (node->length <= 0)
        {
            FOR_NEIGHBOR(node, neighbor_node)
            {
                // NHANLT - DELETE: dummy variable to keep track of the distance of this node to the pruning branch
                neighbor_node->distance_2_pruning = selected_node->distance_2_pruning + 1;
                new_node_stack.push(neighbor_node);
            }
        }
        else
        {
            // now try to place on the current branch below the best node, at an height above or equal to the mid-branch.
            // RealNumType tmp_best_lh_diff = MIN_NEGATIVE;
            SeqRegions* mid_branch_regions = new SeqRegions(node->mid_branch_lh);
            SeqRegions* parent_upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            SeqRegions* lower_regions = node->getPartialLhAtNode(aln, model, threshold_prob);
            RealNumType new_branch_length_split = 0.5 * node->length;
            RealNumType tmp_lh_diff;
            
            while (true)
            {
                tmp_lh_diff = calculateSubTreePlacementCost(mid_branch_regions, subtree_regions, new_branch_length);
                
                // if better placement found -> record it
                if (tmp_lh_diff > best_down_lh_diff)
                {
                    best_down_lh_diff = tmp_lh_diff;
                    best_child = node;
                    best_child_blength_split = new_branch_length_split;
                    new_branch_length_split *= 0.5;
                    
                    replacePartialLH(best_child_regions, mid_branch_regions);
                    
                    if (new_branch_length_split <= half_min_blength_mid)
                        break;
                    
                    // compute mid_branch_regions
                    parent_upper_lr_regions->mergeUpperLower(mid_branch_regions, new_branch_length_split, *lower_regions, node->length - new_branch_length_split, aln, model, threshold_prob);
                }
                else
                    break;
            }
            
            // delete mid_branch_regions
            if (mid_branch_regions) delete mid_branch_regions;
        }
    }
}

void Tree::placeSubTreeAtNode(Node* const selected_node, Node* const subtree, const SeqRegions* const subtree_regions, const RealNumType new_branch_length, const RealNumType new_lh)
{
    // dummy variables
    const StateType num_states = aln.num_states;
    const RealNumType threshold_prob = params->threshold_prob;
    RealNumType best_child_lh;
    RealNumType best_child_blength_split = -1;
    RealNumType best_parent_lh;
    RealNumType best_parent_blength_split = 0;
    SeqRegions* best_parent_regions = NULL;
    RealNumType best_root_blength = -1;
    SeqRegions* best_child_regions = NULL;
    RealNumType best_down_lh_diff = MIN_NEGATIVE;
    Node* best_child = NULL;
    
    // We first explore placement just below the best placement node for more fine-grained placement within its descendant branches (accounting for polytomies).
    handlePolytomyPlaceSubTree(selected_node, subtree_regions, new_branch_length, best_down_lh_diff, best_child, best_child_blength_split, best_child_regions);
    
    // place the new sample as a descendant of an existing node
    if (best_child)
    {
        SeqRegions* upper_left_right_regions = best_child->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_regions = best_child->getPartialLhAtNode(aln, model, threshold_prob);
        best_child_lh = best_down_lh_diff;
        best_child_blength_split = (best_child_blength_split == -1) ? (0.5 * best_child->length) : best_child_blength_split;
        
        // try with a shorter branch length
        tryShorterBranch<&Tree::calculateSubTreePlacementCost>(best_child->length, best_child_regions, subtree_regions, upper_left_right_regions, lower_regions, best_child_lh, best_child_blength_split, new_branch_length, true);
    }
    else
        best_child_lh = MIN_NEGATIVE;
    
    // if node is root, try to place as sibling of the current root.
    RealNumType old_root_lh = MIN_NEGATIVE;
    if (root == selected_node)
    {
        SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
        old_root_lh = lower_regions->computeAbsoluteLhAtRoot(num_states, model);
        
        // merge 2 lower vector into one
        best_parent_lh = lower_regions->mergeTwoLowers(best_parent_regions, default_blength, *subtree_regions, new_branch_length, aln, model, threshold_prob, true);
        
        best_parent_lh += best_parent_regions->computeAbsoluteLhAtRoot(num_states, model);
        
        // Try shorter branch lengths at root
        best_root_blength = default_blength;
        tryShorterBranchAtRoot(subtree_regions, lower_regions, best_parent_regions, best_root_blength, best_parent_lh, new_branch_length);
        
        // update best_parent_lh (taking into account old_root_lh)
        best_parent_lh -= old_root_lh;
    }
    // selected_node is not root
    // try to append just above node
    else
    {
        SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
        best_parent_regions = new SeqRegions(selected_node->mid_branch_lh);
        best_parent_lh = calculateSubTreePlacementCost(best_parent_regions, subtree_regions, new_branch_length);
        best_parent_blength_split = 0.5 * selected_node->length;
        
        // try with a shorter split
        tryShorterBranch<&Tree::calculateSubTreePlacementCost>(selected_node->length, best_parent_regions, subtree_regions, upper_left_right_regions, lower_regions, best_parent_lh, best_parent_blength_split, new_branch_length, false);
    }
    
    // if the best placement is below the selected_node => add an internal node below the selected_node
    /* now we have three likelihood costs,
    best_child_lh is the likelihood score of appending below node;
    best_parent_lh is the likelihood score of appending above node;
    new_lh is the likelihood cost of appending exactly at node. */
    if (best_child_lh >= best_parent_lh && best_child_lh >= new_lh)
    {
        ASSERT(best_child);
        
        SeqRegions* upper_left_right_regions = best_child->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        
        // now try different lengths for the new branch
        RealNumType best_length = new_branch_length;
        estimateLengthNewBranch<&Tree::calculateSubTreePlacementCost>(best_child_lh, best_child_regions, subtree_regions, best_length, max_blength, double_min_blength, (new_branch_length <= 0));
        
        // attach subtree to the phylogenetic tree (below the selected_node ~ above the child node)
        connectSubTree2Branch<&Tree::updateRegionsPlaceSubTree>(subtree_regions, NULL, subtree, best_child, best_child_blength_split, best_child->length - best_child_blength_split, best_length, best_child_regions, upper_left_right_regions);
    }
    // otherwise, add new parent to the selected_node
    else
    {
        SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
        
        // new parent is actually part of a polytomy since best placement is exactly at the node
        if (new_lh >= best_parent_lh)
        {
            best_root_blength = -1;
            best_parent_blength_split = -1;
            best_parent_lh = new_lh;
            if (best_parent_regions) delete best_parent_regions;
            best_parent_regions = NULL;
            
            if (selected_node == root)
                lower_regions->mergeTwoLowers(best_parent_regions, -1, *subtree_regions, new_branch_length, aln, model, threshold_prob);
            else
                best_parent_regions = new SeqRegions(selected_node->total_lh);
        }

        // add parent to the root
        if (selected_node == root)
        {
            // remove old_root_lh from best_parent_lh before estimating the new branch length
            best_parent_lh += old_root_lh;
            
            // estimate the new branch length
            RealNumType best_length2 = new_branch_length;
            estimateLengthNewBranchAtRoot(subtree_regions, lower_regions, best_parent_regions, best_length2, best_parent_lh, best_root_blength, double_min_blength, new_branch_length <= 0);
            
            // update best_parent_lh (taking into account old_root_lh)
            best_parent_lh -= old_root_lh;
            
            // attach subtree to the phylogenetic tree (exactly at the seleted root node)
            connectSubTree2Root(subtree, subtree_regions, lower_regions, selected_node, best_root_blength, best_length2, best_parent_regions);
        }
        //add parent to non-root node (place subtree exactly at the selected non-root node)
        else
        {
            SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            
            // estimate the length for the new branch
            RealNumType best_length = new_branch_length;
            estimateLengthNewBranch<&Tree::calculateSubTreePlacementCost>(best_parent_lh, best_parent_regions, subtree_regions, best_length, new_branch_length * 10, double_min_blength, (new_branch_length <= 0));
            
            // attach subtree to the phylogenetic tree (exactly at the selected non-root node)
            RealNumType down_distance = best_parent_blength_split;
            RealNumType top_distance = selected_node->length - down_distance;
            if (best_parent_blength_split <= 0)
            {
                down_distance = -1;
                top_distance = selected_node->length;
                
                if (selected_node->total_lh) delete selected_node->total_lh;
                selected_node->total_lh = NULL;
                
                if (selected_node->mid_branch_lh) delete selected_node->mid_branch_lh;
                selected_node->mid_branch_lh = NULL;
            }
            
            connectSubTree2Branch<&Tree::updateRegionsPlaceSubTreeAbove>(subtree_regions, lower_regions, subtree, selected_node, top_distance, down_distance, best_length, best_child_regions, upper_left_right_regions);
        }
    }
    
    // delete best_parent_regions and best_child_regions
    if (best_parent_regions)
        delete best_parent_regions;
    if (best_child_regions)
        delete best_child_regions;
}

template <RealNumType(Tree::*calculatePlacementCost)(const SeqRegions* const, const SeqRegions* const, RealNumType)>
bool Tree::tryShorterBranch(const RealNumType current_blength, SeqRegions* &best_child_regions, const SeqRegions* const sample, const SeqRegions* const upper_left_right_regions, const SeqRegions* const lower_regions, RealNumType &best_split_lh, RealNumType &best_branch_length_split, const RealNumType new_branch_length, const bool try_first_branch)
{
    SeqRegions* new_parent_regions = NULL;
    bool found_new_split = false;
    RealNumType new_branch_length_split = 0.5 * best_branch_length_split;
    
    while (new_branch_length_split > min_blength)
    {
        // try on the first or second branch
        if (try_first_branch)
            upper_left_right_regions->mergeUpperLower(new_parent_regions, new_branch_length_split, *lower_regions,  current_blength - new_branch_length_split, aln, model, params->threshold_prob);
        else
            upper_left_right_regions->mergeUpperLower(new_parent_regions, current_blength - new_branch_length_split, *lower_regions, new_branch_length_split, aln, model, params->threshold_prob);
        
        // calculate placement_cost
        RealNumType placement_cost = (this->*calculatePlacementCost)(new_parent_regions, sample, new_branch_length);
        
        if (placement_cost > best_split_lh)
        {
            best_split_lh = placement_cost;
            best_branch_length_split = new_branch_length_split;
            new_branch_length_split *= 0.5;
            found_new_split = true;
            
            replacePartialLH(best_child_regions, new_parent_regions);
        }
        else
            break;
    }
    
    // delete new_parent_regions
    if (new_parent_regions) delete new_parent_regions;
    
    return found_new_split;
}

template <RealNumType(Tree::*calculatePlacementCost)(const SeqRegions* const, const SeqRegions* const, RealNumType)>
bool Tree::tryShorterNewBranch(const SeqRegions* const best_child_regions, const SeqRegions* const sample, RealNumType &best_blength, RealNumType &new_branch_lh, const RealNumType short_blength_thresh)
{
    bool found_new_split = false;
    RealNumType new_blength = best_blength;
    RealNumType placement_cost;
    
    while (best_blength > short_blength_thresh)
    {
        new_blength *= 0.5;
        placement_cost = (this->*calculatePlacementCost)(best_child_regions, sample, new_blength);
        
        if (placement_cost > new_branch_lh)
        {
            new_branch_lh = placement_cost;
            best_blength = new_blength;
            found_new_split = true;
        }
        else
            break;
    }
    
    return found_new_split;
}

template <RealNumType(Tree::*calculatePlacementCost)(const SeqRegions* const, const SeqRegions* const, RealNumType)>
void Tree::tryLongerNewBranch(const SeqRegions* const best_child_regions, const SeqRegions* const sample, RealNumType &best_blength, RealNumType &new_branch_lh, const RealNumType long_blength_thresh)
{
    RealNumType new_blength = best_blength;
    RealNumType placement_cost;
    
    while (best_blength < long_blength_thresh)
    {
        new_blength += new_blength;
        placement_cost = (this->*calculatePlacementCost)(best_child_regions, sample, new_blength);
        if (placement_cost > new_branch_lh)
        {
            new_branch_lh = placement_cost;
            best_blength = new_blength;
        }
        else
            break;
    }
}

template <RealNumType(Tree::*calculatePlacementCost)(const SeqRegions* const, const SeqRegions* const, RealNumType)>
void Tree::estimateLengthNewBranch(const RealNumType best_split_lh, const SeqRegions* const best_child_regions, const SeqRegions* const sample, RealNumType &best_blength, const RealNumType long_blength_thresh, const RealNumType short_blength_thresh, const bool optional_check)
{
    RealNumType new_branch_lh = best_split_lh;
    
    // change zero branch length to min branch length
    if (optional_check)
    {
        best_blength = min_blength;
        new_branch_lh = (this->*calculatePlacementCost)(best_child_regions, sample, best_blength);
    }
    
    // try shorter lengths for the new branch
    bool found_new_blength = tryShorterNewBranch<calculatePlacementCost>(best_child_regions, sample, best_blength, new_branch_lh, min_blength);
    
    // try longer lengths for the new branch
    if (optional_check || !found_new_blength) // (best_blength > 0.7 * default_blength)
        tryLongerNewBranch<calculatePlacementCost>(best_child_regions, sample, best_blength, new_branch_lh, long_blength_thresh);
    
    // try zero-length for the new branch
    if (best_blength < short_blength_thresh)
    {
        RealNumType zero_branch_lh = (this->*calculatePlacementCost)(best_child_regions, sample, -1);
        if (zero_branch_lh > new_branch_lh)
            best_blength = -1;
    }
}

void Tree::connectNewSample2Branch(SeqRegions* const sample, const std::string &seq_name, Node* const sibling_node, const RealNumType top_distance, const RealNumType down_distance, const RealNumType best_blength, SeqRegions* &best_child_regions, const SeqRegions* const upper_left_right_regions)
{
    const RealNumType threshold_prob = params->threshold_prob;
    
    // create new internal node and append child to it
    Node* new_internal_node = new Node(true);
    Node* next_node_1 = new Node();
    Node* next_node_2 = new Node();
    Node* new_sample_node = new Node(seq_name);
    
    new_internal_node->next = next_node_2;
    next_node_2->next = next_node_1;
    next_node_1->next = new_internal_node;
    
    new_internal_node->neighbor = sibling_node->neighbor;
    sibling_node->neighbor->neighbor = new_internal_node;
    new_internal_node->length = top_distance;
    new_internal_node->neighbor->length = top_distance;
    
    sibling_node->neighbor = next_node_2;
    next_node_2->neighbor = sibling_node;
    sibling_node->length = down_distance;
    sibling_node->neighbor->length = down_distance;
    
    new_sample_node->neighbor = next_node_1;
    next_node_1->neighbor = new_sample_node;
    new_sample_node->length = best_blength;
    new_sample_node->neighbor->length = best_blength;
    
    new_sample_node->partial_lh = sample;
    next_node_1->partial_lh = best_child_regions;
    best_child_regions = NULL;
    upper_left_right_regions->mergeUpperLower(next_node_2->partial_lh, new_internal_node->length, *sample, best_blength, aln, model, threshold_prob);
    sibling_node->getPartialLhAtNode(aln, model, threshold_prob)->mergeTwoLowers(new_internal_node->partial_lh, sibling_node->length, *sample, best_blength, aln, model, threshold_prob);
    RealNumType half_branch_length = new_internal_node->length * 0.5;
    upper_left_right_regions->mergeUpperLower(new_internal_node->mid_branch_lh, half_branch_length, *new_internal_node->partial_lh, half_branch_length, aln, model, threshold_prob);
    new_internal_node->computeTotalLhAtNode(aln, model, threshold_prob, new_internal_node == root);
    
    if (!new_internal_node->total_lh || new_internal_node->total_lh->empty())
        outError("Problem, None vector when placing sample, below node");
    
    if (best_blength > 0)
    {
        new_sample_node->computeTotalLhAtNode(aln, model, threshold_prob, new_sample_node == root);
        RealNumType half_branch_length = new_sample_node->length * 0.5;
        next_node_1->getPartialLhAtNode(aln, model, threshold_prob)->mergeUpperLower(new_sample_node->mid_branch_lh, half_branch_length, *sample, half_branch_length, aln, model, threshold_prob);
    }
    
    // update pseudo_count
    model.updatePesudoCount(aln, *next_node_1->getPartialLhAtNode(aln, model, threshold_prob), *sample);

    // iteratively traverse the tree to update partials from the current node
    stack<Node*> node_stack;
    node_stack.push(sibling_node);
    node_stack.push(new_internal_node->neighbor);
    updatePartialLh(node_stack);
}

void Tree::placeNewSampleMidBranch(Node* const selected_node, SeqRegions* const sample, const std::string &seq_name, const RealNumType best_lh_diff)
{
    // dummy variables
    const RealNumType threshold_prob = params->threshold_prob;
    SeqRegions* best_child_regions = NULL;
    
    SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    RealNumType best_split_lh = best_lh_diff;
    RealNumType best_branch_length_split = 0.5 * selected_node->length;
    best_child_regions = new SeqRegions(selected_node->mid_branch_lh);
    SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
    
    // try different positions on the existing branch
    bool found_new_split = tryShorterBranch<&Tree::calculateSamplePlacementCost>(selected_node->length, best_child_regions, sample, upper_left_right_regions, lower_regions, best_split_lh, best_branch_length_split, default_blength, true);
    
    if (!found_new_split)
    {
        // try on the second branch
        found_new_split = tryShorterBranch<&Tree::calculateSamplePlacementCost>(selected_node->length, best_child_regions, sample, upper_left_right_regions, lower_regions, best_split_lh, best_branch_length_split, default_blength, false);
        
        if (found_new_split)
            best_branch_length_split = selected_node->length - best_branch_length_split;
    }
    
    // now try different lengths for the new branch
    RealNumType best_blength = default_blength;
    estimateLengthNewBranch<&Tree::calculateSamplePlacementCost>(best_split_lh, best_child_regions, sample, best_blength, max_blength, min_blength, false);
    
    // create new internal node and append child to it
    connectNewSample2Branch(sample, seq_name, selected_node, best_branch_length_split, selected_node->length - best_branch_length_split, best_blength, best_child_regions, upper_left_right_regions);
    
    // delete best_parent_regions and best_child_regions
    if (best_child_regions)
        delete best_child_regions;
}

void Tree::tryShorterBranchAtRoot(const SeqRegions* const sample, const SeqRegions* const lower_regions, SeqRegions* &best_parent_regions, RealNumType &best_root_blength, RealNumType &best_parent_lh, const RealNumType fixed_blength)
{
    SeqRegions* merged_root_sample_regions = NULL;
    RealNumType new_blength = 0.5 * best_root_blength;
    RealNumType new_root_lh;
    
    while (new_blength > min_blength)
    {
        // merge 2 lower vector into one
        new_root_lh = lower_regions->mergeTwoLowers(merged_root_sample_regions, new_blength, *sample, fixed_blength, aln, model, params->threshold_prob, true);
        new_root_lh += merged_root_sample_regions->computeAbsoluteLhAtRoot(aln.num_states, model);
        
        if (new_root_lh > best_parent_lh)
        {
            best_parent_lh = new_root_lh;
            best_root_blength = new_blength;
            new_blength *= 0.5;
            
            replacePartialLH(best_parent_regions, merged_root_sample_regions);
        }
        else
            break;
    }
    
    // delete merged_root_sample_regions
    if (merged_root_sample_regions) delete merged_root_sample_regions;
}

bool Tree::tryShorterNewBranchAtRoot(const SeqRegions* const sample, const SeqRegions* const lower_regions, SeqRegions* &best_parent_regions, RealNumType &best_length, RealNumType &best_parent_lh, const RealNumType fixed_blength)
{
    SeqRegions* new_root_lower_regions = NULL;
    bool found_new_split = false;
    RealNumType new_blength = best_length;
    RealNumType new_root_lh = 0;
    
    while (best_length > min_blength)
    {
        new_blength *= 0.5;
        
        new_root_lh = lower_regions->mergeTwoLowers(new_root_lower_regions, fixed_blength, *sample, new_blength, aln, model, params->threshold_prob, true);
        new_root_lh += new_root_lower_regions->computeAbsoluteLhAtRoot(aln.num_states, model);
        
        if (new_root_lh > best_parent_lh)
        {
            best_parent_lh = new_root_lh;
            best_length = new_blength;
            found_new_split = true;
            
            replacePartialLH(best_parent_regions, new_root_lower_regions);
        }
        else
            break;
    }
    
    // delete new_root_lower_regions
    if (new_root_lower_regions) delete new_root_lower_regions;
    
    return found_new_split;
}

bool Tree::tryLongerNewBranchAtRoot(const SeqRegions* const sample, const SeqRegions* const lower_regions, SeqRegions* &best_parent_regions, RealNumType &best_length, RealNumType &best_parent_lh, const RealNumType fixed_blength)
{
    SeqRegions* new_root_lower_regions = NULL;
    bool found_new_split = false;
    RealNumType new_blength = best_length;
    RealNumType new_root_lh = 0;
    
    while (best_length < max_blength)
    {
        new_blength += new_blength;
        
        new_root_lh = lower_regions->mergeTwoLowers(new_root_lower_regions, fixed_blength, *sample, new_blength, aln, model, params->threshold_prob, true);
        new_root_lh += new_root_lower_regions->computeAbsoluteLhAtRoot(aln.num_states, model);
        
        if (new_root_lh > best_parent_lh)
        {
            best_parent_lh = new_root_lh;
            best_length = new_blength;
            found_new_split = true;
            
            replacePartialLH(best_parent_regions, new_root_lower_regions);
        }
        else
            break;
    }
    
    // delete new_root_lower_regions
    if (new_root_lower_regions) delete new_root_lower_regions;
    
    return found_new_split;
}

void Tree::estimateLengthNewBranchAtRoot(const SeqRegions* const sample, const SeqRegions* const lower_regions, SeqRegions* &best_parent_regions, RealNumType &best_length, RealNumType &best_parent_lh, const RealNumType fixed_blength, const RealNumType short_blength_thresh, const bool optional_check)
{
    if (optional_check)
    {
        SeqRegions* new_root_lower_regions = NULL;
        best_length = min_blength;
        best_parent_lh = lower_regions->mergeTwoLowers(new_root_lower_regions, fixed_blength, *sample, best_length, aln, model, params->threshold_prob, true);
        
        best_parent_lh += new_root_lower_regions->computeAbsoluteLhAtRoot(aln.num_states, model);
        
        replacePartialLH(best_parent_regions, new_root_lower_regions);
    }
    
    // try shorter lengths
    bool found_new_split = tryShorterNewBranchAtRoot(sample, lower_regions, best_parent_regions, best_length, best_parent_lh, fixed_blength);
    
    // try longer lengths
    if (optional_check || !found_new_split)
        tryLongerNewBranchAtRoot(sample, lower_regions, best_parent_regions, best_length, best_parent_lh, fixed_blength);
    
    
    // try with length zero
    if (best_length < short_blength_thresh)
    {
        SeqRegions* new_root_lower_regions = NULL;
        
        RealNumType new_root_lh = lower_regions->mergeTwoLowers(new_root_lower_regions, fixed_blength, *sample, -1, aln, model, params->threshold_prob, true);
        new_root_lh += new_root_lower_regions->computeAbsoluteLhAtRoot(aln.num_states, model);
        
        if (new_root_lh > best_parent_lh)
        {
            best_length = -1;
            replacePartialLH(best_parent_regions, new_root_lower_regions);
        }
        
        // delete new_root_lower_regions
        if (new_root_lower_regions) delete new_root_lower_regions;
    }
}

void Tree::connectNewSample2Root(SeqRegions* const sample, const std::string &seq_name, Node* const sibling_node, const RealNumType best_root_blength, const RealNumType best_length2, SeqRegions* &best_parent_regions)
{
    const RealNumType threshold_prob = params->threshold_prob;
    
    Node* new_root = new Node(true);
    Node* next_node_1 = new Node();
    Node* next_node_2 = new Node();
    Node* new_sample_node = new Node(seq_name);
    
    new_root->next = next_node_2;
    next_node_2->next = next_node_1;
    next_node_1->next = new_root;
    
    // attach the left child
    sibling_node->neighbor = next_node_2;
    next_node_2->neighbor = sibling_node;
    sibling_node->length = best_root_blength;
    sibling_node->neighbor->length = best_root_blength;
    
    if (best_root_blength <= 0)
    {
        if (sibling_node->total_lh) delete sibling_node->total_lh;
        sibling_node->total_lh = NULL;
        
        if (sibling_node->mid_branch_lh) delete sibling_node->mid_branch_lh;
        sibling_node->mid_branch_lh = NULL;
        //selected_node.furtherMidNodes=None
    }
    
    // attach the right child
    new_sample_node->neighbor = next_node_1;
    next_node_1->neighbor = new_sample_node;
    new_sample_node->length = best_length2;
    new_sample_node->neighbor->length = best_length2;
    
    new_root->partial_lh = best_parent_regions;
    best_parent_regions = NULL;
    new_root->total_lh = new_root->computeTotalLhAtNode(aln, model, threshold_prob, true);

    next_node_1->partial_lh = sibling_node->getPartialLhAtNode(aln, model, threshold_prob)->computeTotalLhAtRoot(aln.num_states, model, best_root_blength);
    next_node_2->partial_lh = sample->computeTotalLhAtRoot(aln.num_states, model, best_length2);
    
    new_sample_node->partial_lh = sample;
    
    if (!new_root->total_lh || new_root->total_lh->size() == 0)
    {
        outError("Problem, None vector when placing sample, new root");
        /*print(merged_root_sample_regions)
        print(node.probVect)
        print(sample)
        print(best_length2)
        print(best_root_blength)*/
    }
    
    if (best_length2 > 0)
    {
        new_sample_node->computeTotalLhAtNode(aln, model, threshold_prob, new_sample_node == root);
        RealNumType half_branch_length = new_sample_node->length * 0.5;
        next_node_1->getPartialLhAtNode(aln, model, threshold_prob)->mergeUpperLower(new_sample_node->mid_branch_lh, half_branch_length, *sample, half_branch_length, aln, model, threshold_prob);
        
        /*if best_length2>=2*min_blengthForMidNode:
            createFurtherMidNodes(new_root.children[1],new_root.probVectUpLeft)*/
    }
    
    // update tree->root;
    root = new_root;
    
    // iteratively traverse the tree to update partials from the current node
    stack<Node*> node_stack;
    node_stack.push(sibling_node);
    updatePartialLh(node_stack);
}

void Tree::placeNewSampleAtNode(Node* const selected_node, SeqRegions* const sample, const std::string &seq_name, const RealNumType best_lh_diff, const RealNumType best_up_lh_diff, const RealNumType best_down_lh_diff, Node* const best_child)
{
    // dummy variables
    RealNumType best_child_lh = MIN_NEGATIVE;
    RealNumType best_child_blength_split = 0;
    RealNumType best_parent_lh;
    RealNumType best_parent_blength_split = 0;
    RealNumType best_root_blength = -1;
    StateType num_states = aln.num_states;
    const RealNumType threshold_prob = params->threshold_prob;
    SeqRegions* best_parent_regions = NULL;
    SeqRegions* best_child_regions = NULL;
    
    // place the new sample as a descendant of an existing node
    if (best_child)
    {
        best_child_lh = best_down_lh_diff;
        best_child_blength_split = 0.5 * best_child->length;
        SeqRegions* upper_left_right_regions = best_child->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_regions = best_child->getPartialLhAtNode(aln, model, threshold_prob);
        // if (best_child_regions) delete best_child_regions;
        best_child_regions = new SeqRegions(best_child->mid_branch_lh);
        
        // try a shorter split
        tryShorterBranch<&Tree::calculateSamplePlacementCost>(best_child->length, best_child_regions, sample, upper_left_right_regions, lower_regions, best_child_lh, best_child_blength_split, default_blength, true);
    }
    
    // if node is root, try to place as sibling of the current root.
    RealNumType old_root_lh = MIN_NEGATIVE;
    if (root == selected_node)
    {
        old_root_lh = selected_node->getPartialLhAtNode(aln, model, threshold_prob)->computeAbsoluteLhAtRoot(num_states, model);
        SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
        
        // merge 2 lower vector into one
        RealNumType new_root_lh = lower_regions->mergeTwoLowers(best_parent_regions, default_blength, *sample, default_blength, aln, model, threshold_prob, true);
        
        new_root_lh += best_parent_regions->computeAbsoluteLhAtRoot(num_states, model);
        best_parent_lh = new_root_lh;
        
        // try shorter branch lengths
        best_root_blength = default_blength;
        tryShorterBranchAtRoot(sample, lower_regions, best_parent_regions, best_root_blength, best_parent_lh, default_blength);
        
        // update best_parent_lh (taking into account old_root_lh)
        best_parent_lh -= old_root_lh;
    }
    // selected_node is not root
    else
    {
        // RealNumType best_split = 0.5;
        // RealNumType best_split_lh = best_up_lh_diff;
        best_parent_lh = best_up_lh_diff;
        best_parent_blength_split = 0.5 * selected_node->length;
        SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
        // if (best_parent_regions) delete best_parent_regions;
        best_parent_regions = new SeqRegions(selected_node->mid_branch_lh);
        
        // try a shorter split
        tryShorterBranch<&Tree::calculateSamplePlacementCost>(selected_node->length, best_parent_regions, sample, upper_left_right_regions, lower_regions, best_parent_lh, best_parent_blength_split, default_blength, false);
    }
    
    // if the best placement is below the selected_node => add an internal node below the selected_node
    if (best_child_lh >= best_parent_lh && best_child_lh >= best_lh_diff)
    {
        ASSERT(best_child);
        
        SeqRegions* upper_left_right_regions = best_child->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        
        // Estimate the length for the new branch
        RealNumType best_length = default_blength;
        estimateLengthNewBranch<&Tree::calculateSamplePlacementCost>(best_child_lh, best_child_regions, sample, best_length, max_blength, min_blength, false);
        
        // create new internal node and append child to it
        connectNewSample2Branch(sample, seq_name, best_child, best_child_blength_split, best_child->length - best_child_blength_split, best_length, best_child_regions, upper_left_right_regions);
    }
    // otherwise, add new parent to the selected_node
    else
    {
        // new parent is actually part of a polytomy since best placement is exactly at the node
        if (best_lh_diff >= best_parent_lh)
        {
            best_root_blength = -1;
            best_parent_blength_split = -1;
            best_parent_lh = best_lh_diff;
            if (best_parent_regions) delete best_parent_regions;
            best_parent_regions = NULL;
            
            if (selected_node == root)
                selected_node->getPartialLhAtNode(aln, model, threshold_prob)->mergeTwoLowers(best_parent_regions, -1, *sample, default_blength, aln, model, threshold_prob);
            else
                best_parent_regions = new SeqRegions(selected_node->total_lh);
        }

        // add parent to the root
        if (selected_node == root)
        {
            // now try different lengths for right branch
            best_parent_lh += old_root_lh;
            RealNumType best_length2 = default_blength;
            const SeqRegions* const lower_regions = selected_node->getPartialLhAtNode(aln, model, threshold_prob);
            
            estimateLengthNewBranchAtRoot(sample, lower_regions, best_parent_regions, best_length2, best_parent_lh, best_root_blength, min_blength, false);
            
            // update best_parent_lh (taking into account old_root_lh)
            best_parent_lh -= old_root_lh;
            
            // add new sample to a new root
            connectNewSample2Root(sample, seq_name, selected_node, best_root_blength, best_length2, best_parent_regions);
        }
        //add parent to non-root node
        else
        {
            SeqRegions* upper_left_right_regions = selected_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
            
            // now try different lengths for the new branch
            RealNumType new_branch_length_lh = best_parent_lh;
            RealNumType best_length = default_blength;
            estimateLengthNewBranch<&Tree::calculateSamplePlacementCost>(best_parent_lh, best_parent_regions, sample, best_length, max_blength, min_blength, false);
            
            // now create new internal node and append child to it
            RealNumType down_distance = best_parent_blength_split;
            RealNumType top_distance = selected_node->length - down_distance;
            if (best_parent_blength_split < 0)
            {
                down_distance = -1;
                top_distance = selected_node->length;
                
                if (selected_node->total_lh) delete selected_node->total_lh;
                selected_node->total_lh = NULL;
                
                if (selected_node->mid_branch_lh) delete selected_node->mid_branch_lh;
                selected_node->mid_branch_lh = NULL;
                
                /*
                node.furtherMidNodes=None*/
            }
            connectNewSample2Branch(sample, seq_name, selected_node, top_distance, down_distance, best_length, best_parent_regions, upper_left_right_regions);
        }
    }
    
    // delete best_parent_regions and best_child_regions
    if (best_parent_regions)
        delete best_parent_regions;
    if (best_child_regions)
        delete best_child_regions;
}

void Tree::refreshAllLhs()
{
    // 1. update all the lower lhs along the tree
    refreshAllLowerLhs();
    
    // 2. update all the non-lower lhs along the tree
    refreshAllNonLowerLhs();
}

void Tree::refreshAllLowerLhs()
{
    // start from root
    Node* node = root;
    Node* last_node = NULL;
    
    // traverse to the deepest tip, update the lower lhs upward from the tips
    while (node)
    {
        // we reach a top node by a downward traversing
        if (node->is_top)
        {
            // if the current node is a leaf -> we reach the deepest tip -> traversing upward to update the lower lh of it parent
            if (node->isLeave())
            {
                last_node = node;
                node = node->neighbor;
            }
            // otherwise, keep traversing downward to find the deepest tip
            else
                node = node->next->neighbor;
        }
        // we reach the current node by an upward traversing from its children
        else
        {
            // if we reach the current node by an upward traversing from its first children -> traversing downward to its second children
            if (node->getTopNode()->next->neighbor == last_node)
                node = node->getTopNode()->next->next->neighbor;
            // otherwise, all children of the current node are updated -> update the lower lh of the current node
            else
            {
                // calculate the new lower lh of the current node from its children
                Node* top_node = node->getTopNode();
                Node* next_node_1 = top_node->next;
                Node* next_node_2 = next_node_1->next;
                
                SeqRegions* new_lower_lh = NULL;
                SeqRegions* lower_lh_1 = next_node_1->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
                SeqRegions* lower_lh_2 = next_node_2->neighbor->getPartialLhAtNode(aln, model, params->threshold_prob);
                lower_lh_1->mergeTwoLowers(new_lower_lh, next_node_1->length, *lower_lh_2, next_node_2->length, aln, model, params->threshold_prob);
                 
                // if new_lower_lh is NULL -> we need to update the branch lengths connecting the current node to its children
                if (!new_lower_lh)
                {
                    if (next_node_1->length <= 0)
                    {
                        stack<Node*> node_stack;
                        // NHANLT: note different from original maple
                        // updateBLen(nodeList,node,mutMatrix) -> the below codes update from next_node_1 instead of top_node
                        updateZeroBlength(next_node_1->neighbor, node_stack, params->threshold_prob);
                        updatePartialLh(node_stack);
                    }
                    else if (next_node_2->length <= 0)
                    {
                        stack<Node*> node_stack;
                        updateZeroBlength(next_node_2->neighbor, node_stack, params->threshold_prob);
                        updatePartialLh(node_stack);
                    }
                    else
                        outError("Strange, branch lengths > 0 but inconsistent lower lh creation in refreshAllLowerLhs()");
                }
                // otherwise, everything is good -> update the lower lh of the current node
                else
                    replacePartialLH(top_node->partial_lh, new_lower_lh);

                // delete new_lower_lh
                if (new_lower_lh)
                    delete new_lower_lh;
                
                // traverse upward to the parent of the current node
                last_node = top_node;
                node = top_node->neighbor;
            }
        }
    }
}

void Tree::refreshUpperLR(Node* const node, Node* const next_node, SeqRegions* &replaced_regions, const SeqRegions &parent_upper_lr_lh)
{
    // recalculate the upper left/right lh of the current node
    const RealNumType threshold_prob = params->threshold_prob;
    SeqRegions* new_upper_lr_lh = NULL;
    SeqRegions* lower_lh = next_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    parent_upper_lr_lh.mergeUpperLower(new_upper_lr_lh, node->length, *lower_lh, next_node->length, aln, model, threshold_prob);
    
    // if the upper left/right lh is null -> try to increase the branch length
    if (!new_upper_lr_lh)
    {
        if (next_node->length <= 0)
        {
            stack<Node*> node_stack;
            updateZeroBlength(next_node->neighbor, node_stack, threshold_prob);
            updatePartialLh(node_stack);
        }
        else if (node->length <= 0)
        {
            stack<Node*> node_stack;
            updateZeroBlength(node, node_stack, threshold_prob);
            updatePartialLh(node_stack);
        }
        else
            outError("Strange, inconsistent upper left/right lh creation in refreshAllNonLowerLhs()");
    }
    // otherwise, everything is good -> update upper left/right lh of the current node
    else
        replacePartialLH(replaced_regions, new_upper_lr_lh);
    
    // delete new_upper_lr_lh
    if (new_upper_lr_lh) delete new_upper_lr_lh;
}

void Tree::refreshNonLowerLhsFromParent(Node* &node, Node* &last_node)
{
    const RealNumType threshold_prob = params->threshold_prob;
    SeqRegions* parent_upper_lr_lh = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    
    // update the total lh, total lh at the mid-branch point of the current node
    if (node->length > 0)
    {
        // update the total lh
        node->computeTotalLhAtNode(aln, model, threshold_prob, node == root);
        if (!node->total_lh)
            outError("Strange, inconsistent total lh creation in refreshAllNonLowerLhs()");

        // update mid_branch_lh
        computeMidBranchRegions(node, node->mid_branch_lh, *parent_upper_lr_lh);
    }
    
    // if the current node is an internal node (~having children) -> update its upper left/right lh then traverse downward to update non-lower lhs of other nodes
    if (!node->isLeave())
    {
        Node* next_node_1 = node->next;
        Node* next_node_2 = next_node_1->next;
        
        // recalculate the FIRST upper left/right lh of the current node
        refreshUpperLR(node, next_node_2, next_node_1->partial_lh, *parent_upper_lr_lh);
        
        // recalculate the SECOND upper left/right lh of the current node
        refreshUpperLR(node, next_node_1, next_node_2->partial_lh, *parent_upper_lr_lh);
        
        // keep traversing downward to its firt child
        node = next_node_1->neighbor;
    }
    // if the current node is a leaf -> traverse upward to its parent
    else
    {
        last_node = node;
        node = node->neighbor;
    }
}

void Tree::refreshAllNonLowerLhs()
{
    // dummy variables
    StateType num_states = aln.num_states;
    RealNumType threshold_prob = params->threshold_prob;
    
    // start from the root
    Node* node = root;
    
    // update the total lh at root
    node->computeTotalLhAtNode(aln, model, params->threshold_prob, true);
    
    // if the root has children -> update its upper left/right lh then traverse downward to update non-lower lhs of other nodes
    if (!node->isLeave())
    {
        // update upper left/right lh of the root
        Node* next_node_1 = node->next;
        Node* next_node_2 = next_node_1->next;
        delete next_node_1->partial_lh;
        next_node_1->partial_lh = next_node_2->neighbor->getPartialLhAtNode(aln, model, threshold_prob)->computeTotalLhAtRoot(num_states, model, next_node_2->length);
        delete next_node_2->partial_lh;
        next_node_2->partial_lh = next_node_1->neighbor->getPartialLhAtNode(aln, model, threshold_prob)->computeTotalLhAtRoot(num_states, model, next_node_1->length);
        
        // traverse the tree downward and update the non-lower genome lists for all other nodes of the tree.
        Node* last_node = NULL;
        node = next_node_1->neighbor;
        while (node)
        {
            // we reach a top node by a downward traversing
            if (node->is_top)
                refreshNonLowerLhsFromParent(node, last_node);
            // we reach the current node by an upward traversing from its children
            else
            {
                Node* top_node = node->getTopNode();
                Node* next_node_1 = top_node->next;
                Node* next_node_2 = next_node_1->next;
                
                // if we reach the current node by an upward traversing from its first children -> traversing downward to its second children
                if (last_node == next_node_1->neighbor)
                    node = next_node_2->neighbor;
                // otherwise, all children of the current node are updated -> update the lower lh of the current node
                else
                {
                    last_node = top_node;
                    node = top_node->neighbor;
                }
            }
        }
    }
}

void Tree::setAllNodeOutdated()
{
    // start from the root
    stack<Node*> node_stack;
    node_stack.push(root);
    
    // NHANLT - DELETE: update depth
    root->depth = 0;
    
    // traverse downward to set all descentdant outdated
    while (!node_stack.empty())
    {
        // pick the top node from the stack
        Node* node = node_stack.top();
        node_stack.pop();
        
        // set the current node outdated
        node->outdated = true;
        
        // traverse downward
        Node* neighbor_node;
        FOR_NEIGHBOR(node, neighbor_node)
        {
            // NHANLT - DELETE: update depth
            neighbor_node->depth = node->depth + 1;
            
            node_stack.push(neighbor_node);
        }
    }
}

/**
    NHANLT - DELETE:
    Get new (actual) depth of a node
 */
unsigned short int Tree::getNewDepth(Node* node)
{
    node = node->getTopNode();
    unsigned short int depth = 0;
    
    // traverse upwards
    while (node != root)
    {
        node = node->neighbor->getTopNode();
        depth++;
    }
    
    return depth;
}

RealNumType Tree::improveEntireTree(bool short_range_search)
{
    // start from the root
    stack<Node*> node_stack;
    node_stack.push(root);
    
    // dummy variables
    RealNumType total_improvement = 0;
    PositionType num_nodes = 0;
    
    // traverse downward the tree
    while (!node_stack.empty())
    {
        // pick the top node from the stack
        Node* node = node_stack.top();
        node_stack.pop();
        
        // add all children of the current nodes to the stack for further traversing later
        Node* neighbor_node = NULL;
        FOR_NEIGHBOR(node, neighbor_node)
            node_stack.push(neighbor_node);
        
        // only process outdated node to avoid traversing the same part of the tree multiple times
        if (node->outdated)
        {
            node->outdated = false;
            
            /*if checkEachSPR:
                root=node
                while root.up!=None:
                    root=root.up
                #print("Pre-SPR tree: "+createBinaryNewick(root))
                oldTreeLK=calculateTreeLikelihood(root,mutMatrix,checkCorrectness=True)
                #print("Pre-SPR tree likelihood: "+str(oldTreeLK))
                reCalculateAllGenomeLists(root,mutMatrix, checkExistingAreCorrect=True)*/
            
            // do SPR moves to improve the tree
            RealNumType improvement = improveSubTree(node, short_range_search);
            
            /*if checkEachSPR:
                #print(" apparent improvement "+str(improvement))
                root=node
                while root.up!=None:
                    root=root.up
                #print("Post-SPR tree: "+createBinaryNewick(root))
                newTreeLK=calculateTreeLikelihood(root,mutMatrix)
                reCalculateAllGenomeLists(root,mutMatrix, checkExistingAreCorrect=True)
                #print("Post-SPR tree likelihood: "+str(newTreeLK))
                if newTreeLK-oldTreeLK < improvement-1.0:
                    print("In startTopologyUpdates, LK score of improvement "+str(newTreeLK)+" - "+str(oldTreeLK)+" = "+str(newTreeLK-oldTreeLK)+" is less than what is supposd to be "+str(improvement))
                    exit()
             */
            
            // update total_improvement
            total_improvement += improvement;
            
            // Show log every 1000 nodes
            num_nodes += 1;
            if (num_nodes % 1000 == 0)
                cout << "Processed topology for " << convertIntToString(num_nodes) << " nodes." << endl;
        }
    }
    
    return total_improvement;
}

PositionType Tree::optimizeBranchLengths()
{
    // start from the root's children
    stack<Node*> node_stack;
    if (!root || !root->next)
        return 0;
    Node* neighbor_node = NULL;
    FOR_NEIGHBOR(root, neighbor_node)
        node_stack.push(neighbor_node);
    
    // dummy variables
    PositionType num_improvement = 0;
    RealNumType threshold_prob = params->threshold_prob;
    
    // traverse downward the tree
    while (!node_stack.empty())
    {
        // pick the top node from the stack
        Node* node = node_stack.top();
        node_stack.pop();
        
        SeqRegions* upper_lr_regions = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_regions = node->getPartialLhAtNode(aln, model, threshold_prob);
        
        // add all children of the current nodes to the stack for further traversing later
        neighbor_node = NULL;
        FOR_NEIGHBOR(node, neighbor_node)
            node_stack.push(neighbor_node);
        
        // only process outdated node to avoid traversing the same part of the tree multiple times
        if (node->outdated)
        {
            // estimate the branch length
            RealNumType best_length = estimateBranchLength(upper_lr_regions, lower_regions);
            
            if (best_length > 0 || node->length > 0)
            {
                RealNumType diff_thresh = 0.01 * best_length;
                if (best_length <= 0 || node->length <= 0 || (node->length > (best_length + diff_thresh)) || (node->length < (best_length - diff_thresh)))
                {
                    node->length = best_length;
                    node->neighbor->length = node->length;
                    ++num_improvement;
                    
                    // update partial likelihood regions
                    stack<Node*> new_node_stack;
                    new_node_stack.push(node);
                    new_node_stack.push(node->neighbor);
                    updatePartialLh(new_node_stack);
                }
            }
        }
    }
    
    return num_improvement;
}

void Tree::estimateBlength_R_O(const SeqRegion* const seq1_region, const SeqRegion* const seq2_region, const RealNumType total_blength, const PositionType end_pos, RealNumType &coefficient, std::vector<RealNumType> &coefficient_vec)
{
    const StateType seq1_state = aln.ref_seq[end_pos];
    RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
    RealNumType coeff0 = seq2_region->getLH(seq1_state);
    RealNumType coeff1 = 0;

    if (seq1_region->plength_observation2root >= 0)
    {
      coeff0 *= model.root_freqs[seq1_state];

      RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq1_state];

      assert(aln.num_states == 4);
      updateCoeffs<4>(model.root_freqs, transposed_mut_mat_row, &(*seq2_region->likelihood)[0], mutation_mat_row, seq1_region->plength_observation2node, coeff0, coeff1);

      coeff1 *= model.root_freqs[seq1_state];
    }
    else
    {
      // NHANLT NOTES:
      // x = seq1_state
      // l = log(1 + q_xx * t + sum(q_xy * t)
      // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)]
      // coeff1 = numerator = q_xx + sum(q_xy)
        assert(aln.num_states == 4);
        coeff1 += dotProduct<4>(&(*seq2_region->likelihood)[0], mutation_mat_row);
    }

    // NHANLT NOTES:
    // l = log(1 + q_xx * t + sum(q_xy * t)
    // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)]
    // coeff0 = denominator = 1 + q_xx * t + sum(q_xy * t)
    if (total_blength > 0)
      coeff0 += coeff1 * total_blength;

    // NHANLT NOTES:
    // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)] = coeff1 / coeff0
    if (coeff1 < 0)
      coefficient += coeff1 / coeff0;
    else
      coefficient_vec.push_back(coeff0 / coeff1);
}

void Tree::estimateBlength_R_ACGT(const SeqRegion* const seq1_region, const StateType seq2_state, const RealNumType total_blength, const PositionType end_pos, std::vector<RealNumType> &coefficient_vec)
{
    if (seq1_region->plength_observation2root >= 0)
    {
        StateType seq1_state = aln.ref_seq[end_pos];
        
        RealNumType coeff1 = model.root_freqs[seq1_state] * model.mutation_mat[model.row_index[seq1_state] + seq2_state];
        RealNumType coeff0 = model.root_freqs[seq2_state] * model.mutation_mat[model.row_index[seq2_state] + seq1_state] * seq1_region->plength_observation2node;
        
        if (total_blength > 0)
            coeff0 += coeff1 * total_blength;
        
        coefficient_vec.push_back(coeff0 / coeff1);
    }
    // NHANLT: add else here, otherwise, coefficient_vec.push_back(total_blength > 0 ? total_blength : 0) is called even when (seq1_region->plength_observation2root >= 0)
    else
        // NHANLT NOTES:
        // l = log(q_xy * t)
        // l' = q_xy / (q_xy * t) = 1 / t
        coefficient_vec.push_back(total_blength > 0 ? total_blength : 0);
}

void Tree::estimateBlength_O_X(const SeqRegion* const seq1_region, const SeqRegion* const seq2_region, const RealNumType total_blength, const PositionType end_pos, RealNumType &coefficient, std::vector<RealNumType> &coefficient_vec)
{
    const StateType num_states = aln.num_states;
    RealNumType coeff0 = 0;
    RealNumType coeff1 = 0;
    
    // 3.1. e1.type = O and e2.type = O
    if (seq2_region->type == TYPE_O)
    {
        RealNumType* mutation_mat_row = model.mutation_mat;
        
        // NHANLT NOTES:
        // l = log(sum_x(1 + q_xx * t + sum_y(q_xy * t)))
        // l' = [sum_x(q_xx + sum_y(q_xy))]/[sum_x(1 + q_xx * t + sum_y(q_xy * t))]
        // coeff1 = numerator = sum_x(q_xx + sum_y(q_xy))
        // coeff0 = denominator = sum_x(1 + q_xx * t + sum_y(q_xy * t))
        for (StateType i = 0; i < num_states; ++i, mutation_mat_row += num_states)
        {
            RealNumType seq1_lh_i = seq1_region->getLH(i);
            coeff0 += seq1_lh_i * seq2_region->getLH(i);
            
            for (StateType j = 0; j < num_states; ++j)
                coeff1 += seq1_lh_i * seq2_region->getLH(j) * mutation_mat_row[j];
        }
    }
    // 3.2. e1.type = O and e2.type = R or A/C/G/T
    else
    {
        StateType seq2_state = seq2_region->type;
        if (seq2_state == TYPE_R)
            seq2_state = aln.ref_seq[end_pos];
        
        coeff0 = seq1_region->getLH(seq2_state);

        // NHANLT NOTES:
        // y = seq2_state
        // l = log(1 + q_yy * t + sum_x(q_xy * t)
        // l' = [q_yy + sum_x(q_xy))]/[1 + q_xx * t + sum_y(q_xy * t)]
        // coeff1 = numerator = q_yy + sum_x(q_xy))
        // coeff0 = denominator = 1 + q_xx * t + sum_y(q_xy * t)
        RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq2_state];
        assert(num_states == 4);
        coeff1 += dotProduct<4>(&(*seq1_region->likelihood)[0], transposed_mut_mat_row);
    }
    
    if (total_blength > 0)
        coeff0 += coeff1 * total_blength;
    
    // NHANLT NOTES:
    // l' = coeff1 / coeff0
    if (coeff1 < 0)
        coefficient += coeff1 / coeff0;
    else
        coefficient_vec.push_back(coeff0 / coeff1);
}

void Tree::estimateBlength_ACGT_O(const SeqRegion* const seq1_region, const SeqRegion* const seq2_region, const RealNumType total_blength, RealNumType &coefficient, std::vector<RealNumType> &coefficient_vec)
{
    StateType seq1_state = seq1_region->type;
    RealNumType coeff0 = seq2_region->getLH(seq1_state);
    RealNumType coeff1 = 0;
    
    RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
    
    if (seq1_region->plength_observation2root >= 0)
    {
        coeff0 *= model.root_freqs[seq1_state];

        RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq1_state];
                            
        assert(aln.num_states == 4);
        updateCoeffs<4>(model.root_freqs, transposed_mut_mat_row, &(*seq2_region->likelihood)[0], mutation_mat_row, seq1_region->plength_observation2node, coeff0, coeff1);
        
        coeff1 *= model.root_freqs[seq1_state];
    }
    else
    {
        // NHANLT NOTES:
        // x = seq1_state
        // l = log(1 + q_xx * t + sum(q_xy * t)
        // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)]
        // coeff1 = numerator = q_xx + sum(q_xy)
        assert(aln.num_states == 4);
        coeff1 += dotProduct<4>(&(*seq2_region->likelihood)[0], mutation_mat_row);
    }
    
    // NHANLT NOTES:
    // l = log(1 + q_xx * t + sum(q_xy * t)
    // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)]
    // coeff0 = denominator = 1 + q_xx * t + sum(q_xy * t)
    if (total_blength > 0)
        coeff0 += coeff1 * total_blength;
    
    // NHANLT NOTES:
    // l' = [q_xx + sum(q_xy)]/[1 + q_xx * t + sum(q_xy * t)] = coeff1 / coeff0;
    if (coeff1 < 0)
        coefficient += coeff1 / coeff0;
    else
        coefficient_vec.push_back(coeff0 / coeff1);
}

void Tree::estimateBlength_ACGT_RACGT(const SeqRegion* const seq1_region, const SeqRegion* const seq2_region, const RealNumType total_blength, const PositionType end_pos, std::vector<RealNumType> &coefficient_vec)
{
    RealNumType coeff0 = 0;
    StateType seq1_state = seq1_region->type;
    StateType seq2_state = seq2_region->type;
    if (seq2_state == TYPE_R)
        seq2_state = aln.ref_seq[end_pos];
    
    if (seq1_region->plength_observation2root >= 0)
    {
        coeff0 = model.root_freqs[seq2_state] * model.mutation_mat[model.row_index[seq2_state] + seq1_state] * seq1_region->plength_observation2node;
        RealNumType coeff1 = model.root_freqs[seq1_state] * model.mutation_mat[model.row_index[seq1_state] + seq2_state];
        
        if (total_blength > 0)
            coeff0 += coeff1 * total_blength;
        
        coeff0 /= coeff1;
    }
    // NHANLT NOTES:
    // l = log(q_xy * t)
    // l' = q_xy / (q_xy * t) = 1 / t
    else if (total_blength > 0)
        coeff0 = total_blength;
    
    coefficient_vec.push_back(coeff0);
}

RealNumType Tree::estimateBlengthFromCoeffs(RealNumType &coefficient, const std::vector<RealNumType> coefficient_vec)
{
    coefficient = -coefficient;
    PositionType num_coefficients = coefficient_vec.size();
    if (num_coefficients == 0)
        return -1;

    // Get min and max coefficients
    RealNumType min_coefficient = coefficient_vec[0];
    RealNumType max_coefficient = coefficient_vec[0];
    for (PositionType i = 1; i < num_coefficients; ++i)
    {
        RealNumType coefficient_i = coefficient_vec[i];
        if (coefficient_i < min_coefficient)
            min_coefficient = coefficient_i;
        if (coefficient_i > max_coefficient)
            max_coefficient = coefficient_i;
    }
    
    RealNumType num_coefficients_over_coefficient = num_coefficients / coefficient;
    RealNumType tDown = num_coefficients_over_coefficient - min_coefficient;
    if (tDown <= 0)
        return 0;
    RealNumType derivative_tDown = calculateDerivative(coefficient_vec, tDown);
    
    RealNumType tUp = num_coefficients_over_coefficient - max_coefficient;
    if (tUp < 0)
    {
        if (min_coefficient > 0)
            tUp = 0;
        else
            tUp = min_blength_sensitivity;
    }
    RealNumType derivative_tUp = calculateDerivative(coefficient_vec, tUp);
    
    if ((derivative_tDown > coefficient + min_blength_sensitivity) || (derivative_tUp < coefficient - min_blength_sensitivity))
        if ((derivative_tUp < coefficient - min_blength_sensitivity) && (tUp == 0))
            return 0;
    
    while (tDown - tUp > min_blength_sensitivity)
    {
        RealNumType tMiddle = (tUp + tDown) * 0.5;
        RealNumType derivative_tMiddle = calculateDerivative(coefficient_vec, tMiddle);
        
        if (derivative_tMiddle > coefficient)
            tUp = tMiddle;
        else
            tDown = tMiddle;
    }
    
    return tUp;
}

using DoubleState = uint16_t;
static constexpr DoubleState RR = (DoubleState(TYPE_R) << 8) | TYPE_R;
static constexpr DoubleState RO = (DoubleState(TYPE_R) << 8) | TYPE_O;
static constexpr DoubleState OO = (DoubleState(TYPE_O) << 8) | TYPE_O;

RealNumType Tree::estimateBranchLength(const SeqRegions* const parent_regions, const SeqRegions* const child_regions)
{
    // init dummy variables
    RealNumType coefficient = 0;
    vector<RealNumType> coefficient_vec;
    PositionType pos = 0;
    const SeqRegions& seq1_regions = *parent_regions;
    const SeqRegions& seq2_regions = *child_regions;
    size_t iseq1 = 0;
    size_t iseq2 = 0;
    const PositionType seq_length = aln.ref_seq.size();
    const StateType num_states = aln.num_states;
    const RealNumType* const &cumulative_rate = model.cumulative_rate;
    
    // avoid reallocations
    coefficient_vec.reserve(parent_regions->countSharedSegments(seq2_regions, seq_length)); // avoid realloc of vector data


    while (pos < seq_length)
    {
        PositionType end_pos;
        RealNumType total_blength;
        
        // get the next shared segment in the two sequences
        SeqRegions::getNextSharedSegment(pos, seq1_regions, seq2_regions, iseq1, iseq2, end_pos);
        const auto* seq1_region = &seq1_regions[iseq1];
        const auto* seq2_region = &seq2_regions[iseq2];
        
        // 1. e1.type = N || e2.type = N
        if ((seq2_region->type == TYPE_N) + (seq1_region->type == TYPE_N))
        {
            pos = end_pos + 1;
            continue;
        }
        
        // e1.type != N && e2.type != N
        const DoubleState s1s2 = (DoubleState(seq1_region->type) << 8) | seq2_region->type;

        // total_blength will be here the total length from the root or from the upper node, down to the down node.
        if (seq1_region->plength_observation2root >= 0)
            total_blength = seq1_region->plength_observation2root;
        else if (seq1_region->plength_observation2node >= 0)
            total_blength = seq1_region->plength_observation2node;
        else
            total_blength = 0;
            
        if (seq2_region->plength_observation2node >= 0)
            total_blength = total_blength + seq2_region->plength_observation2node;
            //total_blength = (total_blength > 0 ? total_blength : 0) + seq2_region->plength_observation2node;
        
        // 2. e1.type = R
        // 2.1.e1.type = R and e2.type = R
        if (s1s2 == RR)
        {
            // NHANLT NOTES:
            // coefficient = derivative of log likelihood function wrt t
            // l = log(1 + q_xx * t) ~ q_xx * t
            // => l' = q_xx
            //if (seq2_region->type == TYPE_R)
              coefficient += cumulative_rate[end_pos + 1] - cumulative_rate[pos];
        }
        // 2.2. e1.type = R and e2.type = O
        else if (s1s2 == RO)
        {
            estimateBlength_R_O(seq1_region, seq2_region, total_blength, end_pos, coefficient, coefficient_vec);
        }
        // 2.3. e1.type = R and e2.type = A/C/G/T
        else if (seq1_region->type == TYPE_R)
        {
            estimateBlength_R_ACGT(seq1_region, seq2_region->type, total_blength, end_pos, coefficient_vec);
        }
        // 3. e1.type = O
        else if (seq1_region->type == TYPE_O)
        {
            estimateBlength_O_X(seq1_region, seq2_region, total_blength, end_pos, coefficient, coefficient_vec);
        }
        // 4. e1.type = A/C/G/T
        // 4.1. e1.type =  e2.type
        // NHANLT NOTES:
        // coefficient = derivative of log likelihood function wrt t
        // l = log(1 + q_xx * t) ~ q_xx * t
        // => l' = q_xx
        else if (seq1_region->type == seq2_region->type)
            coefficient += model.diagonal_mut_mat[seq1_region->type];
        // e1.type = A/C/G/T and e2.type = O/A/C/G/T
        // 4.2. e1.type = A/C/G/T and e2.type = O
        else if (seq2_region->type == TYPE_O)
        {
            estimateBlength_ACGT_O(seq1_region, seq2_region, total_blength, coefficient, coefficient_vec);
        }
        // 4.3. e1.type = A/C/G/T and e2.type = R or A/C/G/T
        else
        {
            estimateBlength_ACGT_RACGT(seq1_region, seq2_region, total_blength, end_pos, coefficient_vec);
        }
        
        // update pos
        pos = end_pos + 1;
    }
    
    // now optimized branch length based on coefficients
    return estimateBlengthFromCoeffs(coefficient, coefficient_vec);
}

RealNumType Tree::calculateDerivative(const vector<RealNumType> &coefficient_vec, const RealNumType delta_t)
{
    RealNumType result = 0;
    
    for (RealNumType coefficient : coefficient_vec)
        result += 1.0 / (coefficient + delta_t);
        
    return result;
}

void Tree::handleBlengthChanged(Node* const node, const RealNumType best_blength)
{
    node->length = best_blength;
    node->neighbor->length = node->length;
    
    stack<Node*> node_stack;
    node_stack.push(node);
    node_stack.push(node->neighbor);
    updatePartialLh(node_stack);
}

void Tree::optimizeBlengthBeforeSeekingSPR(Node* const node, RealNumType &best_blength, RealNumType &best_lh, bool &blength_changed, const SeqRegions* const parent_upper_lr_lh, const SeqRegions* const lower_lh)
{
    RealNumType original_lh = best_lh;
    
    // try different branch lengths for the current node placement (just in case branch length can be improved, in which case it counts both as tree improvment and better strategy to find a new placement).
    if (node->length <= 0)
    {
        best_blength = min_blength;
        best_lh = calculateSubTreePlacementCost(parent_upper_lr_lh, lower_lh, best_blength);
    }
    
    // cache best_blength
    const RealNumType cached_blength = best_blength;

    // try shorter branch lengths
    bool found_new_blength = tryShorterNewBranch<&Tree::calculateSubTreePlacementCost>(parent_upper_lr_lh, lower_lh, best_blength, best_lh, double_min_blength);
    
    // try longer branch lengths
    if (!found_new_blength)
        tryLongerNewBranch<&Tree::calculateSubTreePlacementCost>(parent_upper_lr_lh, lower_lh, best_blength, best_lh, half_max_blength);
    
    // update blength_changed
    if (cached_blength != best_blength)
        blength_changed = true;
    
    if (node->length <= 0 && original_lh > best_lh)
        best_lh = original_lh;
}

void Tree::checkAndApplySPR(const RealNumType best_lh_diff, const RealNumType best_blength, const RealNumType best_lh, Node* const node, Node* const best_node, Node* const parent_node, const bool is_mid_node, RealNumType& total_improvement, bool& topology_updated)
{
    if (best_node == parent_node)
        outWarning("Strange, re-placement is at same node");
    else if ((best_node == parent_node->next->neighbor || best_node == parent_node->next->next->neighbor) && is_mid_node)
        cout << "Re-placement is above sibling node";
    else [[likely]]
    {
        // reach the top of a multifurcation, which is the only place in a multifurcatio where placement is allowed.
        Node* top_polytomy = best_node;
        while (top_polytomy->length <= 0 && top_polytomy!= root)
            top_polytomy = top_polytomy->neighbor->getTopNode();
        
        if (top_polytomy != best_node)
            outWarning("Strange, placement node not at top of polytomy");
        
        // reach the top of the multifurcation of the parent
        Node* parent_top_polytomy = parent_node;
        while (parent_top_polytomy->length <= 0 && parent_top_polytomy != root)
            parent_top_polytomy = parent_top_polytomy->neighbor->getTopNode();
        
        if (!(parent_top_polytomy == top_polytomy && !is_mid_node))
        {
            total_improvement = best_lh_diff - best_lh;
            
            if (verbose_mode == VB_DEBUG)
                cout << "In improveSubTree() found SPR move with improvement " << total_improvement << endl;
            
            // apply an SPR move
            applySPR(node, best_node, is_mid_node, best_blength, best_lh_diff);
            
            topology_updated = true;
        }
    }
}

RealNumType Tree::improveSubTree(Node* node, bool short_range_search)
{
    // dummy variables
    const RealNumType threshold_prob = params->threshold_prob;
    const RealNumType thresh_placement_cost = short_range_search ? params->thresh_placement_cost_short_search : params->thresh_placement_cost;
    RealNumType total_improvement = 0;
    bool blength_changed = false; // true if a branch length has been changed
    
    // we avoid the root node since it cannot be re-placed with SPR moves
    if (node != root)
    {
        // evaluate current placement
        SeqRegions* parent_upper_lr_lh = node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
        SeqRegions* lower_lh = node->getPartialLhAtNode(aln, model, threshold_prob);
        RealNumType best_blength = node->length;
        RealNumType best_lh = calculateSubTreePlacementCost(parent_upper_lr_lh, lower_lh, best_blength);
        
        // optimize branch length
        if (best_lh < thresh_placement_cost)
            optimizeBlengthBeforeSeekingSPR(node, best_blength, best_lh, blength_changed, parent_upper_lr_lh, lower_lh);
           
        // find new placement
        if (best_lh < thresh_placement_cost)
        {
            // now find the best place on the tree where to re-attach the subtree rooted at "node" but to do that we need to consider new vector probabilities after removing the node that we want to replace this is done using findBestParentTopology().
            bool topology_updated = false;
            Node* parent_node = node->neighbor->getTopNode();
            Node* best_node = NULL;
            RealNumType best_lh_diff = best_lh;
            bool is_mid_node = false;
            RealNumType best_up_lh_diff = MIN_NEGATIVE;
            RealNumType best_down_lh_diff = MIN_NEGATIVE;
            Node* best_child = NULL;
            
            // seek a new placement for the subtree
            seekSubTreePlacement(best_node, best_lh_diff, is_mid_node, best_up_lh_diff, best_down_lh_diff, best_child, short_range_search, node, best_blength, true, NULL);
            
            // validate the new placement cost
            if (best_lh_diff > params->threshold_prob2)
                outError("Strange, lh cost is positive");
            else if (best_lh_diff < -1e50)
                outError("Likelihood cost is very heavy, this might mean that the reference used is not the same used to generate the input diff file");
            
            if (best_lh_diff + thresh_placement_cost > best_lh)
            {
                // check and apply SPR move
                checkAndApplySPR(best_lh_diff, best_blength, best_lh, node, best_node, parent_node, is_mid_node, total_improvement, topology_updated);
                
                if (!topology_updated && blength_changed)
                    handleBlengthChanged(node, best_blength);
            }
            else if (blength_changed)
                handleBlengthChanged(node, best_blength);
        }
        else if (blength_changed)
            handleBlengthChanged(node, best_blength);
    }
                        
    return total_improvement;
}

void calculateSubtreeCost_R_R(const SeqRegion& seq1_region, const RealNumType* const &cumulative_rate, RealNumType& total_blength, const PositionType pos, const PositionType end_pos, RealNumType& lh_cost)
{
    if (seq1_region.plength_observation2root >= 0)
      total_blength += seq1_region.plength_observation2node;

    // NHANLT NOTE:
    // approximation log(1+x)~x
    if (total_blength > 0)
      lh_cost += total_blength * (cumulative_rate[end_pos + 1] - cumulative_rate[pos]);
}

template <const StateType num_states>
void calculateSubtreeCost_R_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType total_blength, const StateType seq1_state, RealNumType& total_factor, const Model& model)
{
    RealNumType tot = 0;
    
    if (seq1_region.plength_observation2root >= 0)
    {
        RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq1_state];
        RealNumType* mutation_mat_row = model.mutation_mat;
                            
        for (StateType i = 0; i < num_states; ++i, mutation_mat_row += num_states)
        {
            // NHANLT NOTE: UNSURE
            // tot2: likelihood that we can observe seq1_state elvoving from i at root (account for the fact that the observation might have occurred on the other side of the phylogeny with respect to the root)
            // tot2 = root_freqs[seq1_state] * (1 + mut[seq1_state,seq1_state] * plength_observation2node) + root_freqs[i] * mut[i,seq1_state] * plength_observation2node
            RealNumType tot2 = model.root_freqs[i] * transposed_mut_mat_row[i] * seq1_region.plength_observation2node + (seq1_state == i ? model.root_freqs[i] : 0);
            
            // NHANLT NOTE:
            // tot3: likelihood of i evolves to j
            // tot3 = (1 + mut[i,i] * total_blength) * lh(seq2,i) + mut[i,j] * total_blength * lh(seq2,j)
            RealNumType tot3 = total_blength > 0 ? (total_blength * dotProduct<num_states>(mutation_mat_row, &((*seq2_region.likelihood)[0]))) : 0;
            
            // NHANLT NOTE:
            // tot = tot2 * tot3
            tot += tot2 * (seq2_region.getLH(i) + tot3);
        }
        
        // NHANLT NOTE: UNCLEAR
        // why we need to divide tot by root_freqs[seq1_state]
        tot *= model.inverse_root_freqs[seq1_state];
    }
    else
    {
        // NHANLT NOTE:
        // (1 + mut[seq1_state,seq1_state] * total_blength) * lh(seq2,seq1_state) + mut[seq1_state,j] * total_blength * lh(seq2,j)
        if (total_blength > 0)
        {
            const RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
            tot += dotProduct<num_states>(mutation_mat_row, &((*seq2_region.likelihood)[0]));
            tot *= total_blength;
        }
        tot += seq2_region.getLH(seq1_state);
    }
    total_factor *= tot;
}

bool calculateSubtreeCost_R_ACGT(const SeqRegion& seq1_region, const RealNumType total_blength, const StateType seq1_state, const StateType seq2_state, RealNumType& total_factor, const Model& model)
{
    if (seq1_region.plength_observation2root >= 0)
    {
        if (total_blength > 0)
        {
            // NHANLT NOTE: UNSURE
            // seq1_state_evolves_seq2_state = (1) the likelihood that seq1_state evolves to seq2_state * (2) the likelihood that seq1_state unchanges from the observing position
            // (1) = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength
            // (2) = (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node)
            RealNumType seq1_state_evolves_seq2_state = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength * (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node);
            
            // NHANLT NOTE: UNCLEAR
            // consider the inverse process of the above
            // seq2_state_evolves_seq1_state = (1) the likelihood that seq2_state evolves to seq1_state * (2) the likelihood that seq2_state unchanges from the observing position
            // (1) = root_freqs[seq2_state] / root_freqs[seq1_state] * mutation_mat[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node
            // (2) = (1.0 + model.diagonal_mut_mat[seq2_state] * total_blength)
            RealNumType seq2_state_evolves_seq1_state = model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node * (1.0 + model.diagonal_mut_mat[seq2_state] * total_blength);
            
            total_factor *= seq1_state_evolves_seq2_state + seq2_state_evolves_seq1_state;
        }
        // NHANLT NOTE:
        // the same as above but total_blength = 0 then we simplify the formula to save the runtime (avoid multiplying with 0)
        else
            total_factor *= model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node;
    }
    // NHANLT NOTE:
    // add the likelihood that seq1_state evoles to seq2_state = mut[seq1_state,seq2_state] * total_blength
    else if (total_blength > 0)
        total_factor *= model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength;
    else
        return false; // return MIN_NEGATIVE;
    
    // no error
    return true;
}

template <const StateType num_states>
void calculateSubtreeCost_O_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType total_blength, RealNumType& total_factor, const Model& model)
{
    if (total_blength > 0)
    {
      total_factor *= matrixEvolve<num_states>(&((*seq1_region.likelihood)[0]), &((*seq2_region.likelihood)[0]), model.mutation_mat, total_blength);
    }
    // NHANLT NOTE:
    // the same as above but total_blength = 0 then we simplify the formula to save the runtime (avoid multiplying with 0)
    else
    {
      total_factor *= dotProduct<num_states>(&((*seq1_region.likelihood)[0]), &((*seq2_region.likelihood)[0]));
    }
}

template <const StateType num_states>
void calculateSubtreeCost_O_RACGT(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType total_blength, const PositionType end_pos, RealNumType& total_factor, const Alignment& aln, const Model& model)
{
    StateType seq2_state = seq2_region.type;
    if (seq2_state == TYPE_R)
        seq2_state = aln.ref_seq[end_pos];
    
    if (total_blength > 0)
    {
        // NHANLT NOTE:
        // tot2: likelihood of i evolves to seq2_state
        // tot2 = (1 + mut[seq2_state,seq2_state] * total_blength) * lh(seq1,seq2_state) + lh(seq1,i) * mut[i,seq2_state] * total_blength
        RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq2_state];
        RealNumType tot2 = dotProduct<num_states>(&((*seq1_region.likelihood)[0]), transposed_mut_mat_row);
        total_factor *= seq1_region.getLH(seq2_state) + total_blength * tot2;
    }
    // NHANLT NOTE:
    // the same as above but total_blength = 0 then we simplify the formula to save the runtime (avoid multiplying with 0)
    else
        total_factor *= seq1_region.getLH(seq2_state);
}

void calculateSubtreeCost_identicalACGT(const SeqRegion& seq1_region, RealNumType& total_blength, RealNumType& lh_cost, const Model& model)
{
    if (seq1_region.plength_observation2root >= 0)
        total_blength += seq1_region.plength_observation2node;
    
    // NHANLT NOTE:
    // the likelihood that seq1_state unchanges
    if (total_blength > 0)
        lh_cost += model.diagonal_mut_mat[seq1_region.type] * total_blength;
}

template <const StateType num_states>
void calculateSubtreeCost_ACGT_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType total_blength, RealNumType& total_factor, const Model& model)
{
    StateType seq1_state = seq1_region.type;
    if (seq1_region.plength_observation2root >= 0)
    {
        RealNumType* transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq1_state];
        RealNumType* mutation_mat_row = model.mutation_mat;
        RealNumType tot = matrixEvolveRoot<num_states>(&((*seq2_region.likelihood)[0]), seq1_state,
          model.root_freqs, transposed_mut_mat_row, mutation_mat_row, total_blength, seq1_region.plength_observation2node);
        // NHANLT NOTE: UNCLEAR
        // why we need to divide tot by root_freqs[seq1_state]
        total_factor *= (tot * model.inverse_root_freqs[seq1_state]);
    }
    else
    {
        RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
        
        // NHANLT NOTE:
        // tot = the likelihood of seq1_state evolving to j
        // (1 + mut[seq1_state,seq1_state] * total_blength) * lh(seq2,seq1_state) + mut[seq1_state,j] * total_blength * lh(seq2,j)
        RealNumType tot = dotProduct<num_states>(mutation_mat_row, &((*seq2_region.likelihood)[0]));
        tot *= total_blength;
        tot += seq2_region.getLH(seq1_state);
        total_factor *= tot;
    }
}

bool calculateSubtreeCost_ACGT_RACGT(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType total_blength, const PositionType end_pos, RealNumType& total_factor, const Alignment& aln, const Model& model)
{
    StateType seq1_state = seq1_region.type;
    StateType seq2_state = seq2_region.type;
    if (seq2_state == TYPE_R)
        seq2_state = aln.ref_seq[end_pos];
    
    if (seq1_region.plength_observation2root >= 0)
    {
        if (total_blength > 0)
        {
            // NHANLT NOTE: UNSURE
            // seq1_state_evolves_seq2_state = (1) the likelihood that seq1_state evolves to seq2_state * (2) the likelihood that seq1_state unchanges from the observing position
            // (1) = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength
            // (2) = (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node)
            RealNumType seq1_state_evolves_seq2_state = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength * (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node);
            
            // NHANLT NOTE: UNCLEAR
            // consider the inverse process of the above
            // seq2_state_evolves_seq1_state = (1) the likelihood that seq2_state evolves to seq1_state * (2) the likelihood that seq2_state unchanges from the observing position
            // (1) = root_freqs[seq2_state] / root_freqs[seq1_state] * mutation_mat[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node
            // (2) = (1.0 + model.diagonal_mut_mat[seq2_state] * total_blength)
            RealNumType seq2_state_evolves_seq1_state = model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node * (1.0 + model.diagonal_mut_mat[seq2_state] * total_blength);
            
            total_factor *= seq1_state_evolves_seq2_state + seq2_state_evolves_seq1_state;
        }
        // NHANLT NOTE:
        // the same as above but total_blength = 0 then we simplify the formula to save the runtime (avoid multiplying with 0)
        else
            total_factor *= model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node;
    }
    // NHANLT NOTE:
    // add the likelihood that seq1_state evoles to seq2_state = mut[seq1_state,seq2_state] * total_blength
    else if (total_blength > 0)
        total_factor *= model.mutation_mat[model.row_index[seq1_state] + seq2_state] * total_blength;
    else
        return false; // return MIN_NEGATIVE;
    
    // no error
    return true;
}

RealNumType Tree::calculateSubTreePlacementCost(const SeqRegions* const parent_regions, const SeqRegions* const child_regions, RealNumType blength)
{
    return (this->*calculateSubTreePlacementCostPointer)(parent_regions, child_regions, blength);
}

// this implementation derives from appendProbNode
template <const StateType num_states>
RealNumType Tree::calculateSubTreePlacementCostTemplate(
  const SeqRegions* const parent_regions, 
  const SeqRegions* const child_regions, 
  RealNumType blength)
{  // 55% of runtime
    // init dummy variables
    RealNumType lh_cost = 0;
    PositionType pos = 0;
    RealNumType total_factor = 1;
    const SeqRegions& seq1_regions = *parent_regions;
    const SeqRegions& seq2_regions = *child_regions;
    size_t iseq1 = 0;
    size_t iseq2 = 0;
    const PositionType seq_length = aln.ref_seq.size();
    
    while (pos < seq_length)
    {
        PositionType end_pos;
        RealNumType total_blength;
        
        // get the next shared segment in the two sequences
        SeqRegions::getNextSharedSegment(pos, seq1_regions, seq2_regions, iseq1, iseq2, end_pos);
        const auto* const seq1_region = &seq1_regions[iseq1];
        const auto* const seq2_region = &seq2_regions[iseq2];

        // 1. e1.type = N || e2.type = N
        if ((seq2_region->type == TYPE_N) + (seq1_region->type == TYPE_N))
        {
            pos = end_pos + 1;
            continue;
        }
       
        // e1.type != N && e2.type != N
        const DoubleState s1s2 = (DoubleState(seq1_region->type) << 8) | seq2_region->type;
        
        // total_blength will be here the total length from the root or from the upper node, down to the down node.
        if (seq1_region->plength_observation2root >= 0)
            total_blength = seq1_region->plength_observation2root + (blength >= 0 ? blength : 0);
        else if (seq1_region->plength_observation2node >= 0)
            total_blength = seq1_region->plength_observation2node + (blength >= 0 ? blength : 0);
        else
            total_blength = blength;
            
        if (seq2_region->plength_observation2node >= 0)
            total_blength = (total_blength > 0 ? total_blength : 0) + seq2_region->plength_observation2node;
        
        
        //assert(total_blength >= 0); // can be -1 ..

        // 2.1. e1.type = R and e2.type = R
        if (s1s2 == RR) [[likely]]
        {
            calculateSubtreeCost_R_R(*seq1_region, model.cumulative_rate, total_blength, pos, end_pos, lh_cost);
        }
        // 2.2. e1.type = R and e2.type = O
        else if (s1s2 == RO)
        {
            calculateSubtreeCost_R_O<num_states>(*seq1_region, *seq2_region, total_blength, aln.ref_seq[end_pos], total_factor, model);
        }
        // 2.3. e1.type = R and e2.type = A/C/G/T
        else if (seq1_region->type == TYPE_R)
        {
            if (!calculateSubtreeCost_R_ACGT(*seq1_region, total_blength, aln.ref_seq[end_pos], seq2_region->type, total_factor, model)) return MIN_NEGATIVE;
        }
        // 3. e1.type = O
        // 3.1. e1.type = O and e2.type = O
        else if (s1s2 == OO)
        {
            calculateSubtreeCost_O_O<num_states>(*seq1_region, *seq2_region, total_blength, total_factor, model);
        }
        // 3.2. e1.type = O and e2.type = R or A/C/G/T
        else if (seq1_region->type == TYPE_O)
        {
            calculateSubtreeCost_O_RACGT<num_states>(*seq1_region, *seq2_region, total_blength, end_pos, total_factor, aln, model);
        }
        // 4. e1.type = A/C/G/T
        // 4.1. e1.type =  e2.type
        else if (seq1_region->type == seq2_region->type)
        {
            calculateSubtreeCost_identicalACGT(*seq1_region, total_blength, lh_cost, model);
        }
        // e1.type = A/C/G/T and e2.type = O/A/C/G/T
        // 4.2. e1.type = A/C/G/T and e2.type = O
        else if (seq2_region->type == TYPE_O)
        {
            calculateSubtreeCost_ACGT_O<num_states>(*seq1_region, *seq2_region, total_blength, total_factor, model);
        }
        // 4.3. e1.type = A/C/G/T and e2.type = R or A/C/G/T
        else
        {
            if (!calculateSubtreeCost_ACGT_RACGT(*seq1_region, *seq2_region, total_blength, end_pos, total_factor, aln, model)) return MIN_NEGATIVE;
        }
         
        // avoid underflow on total_factor
        // approximately update lh_cost and total_factor
        if (total_factor <= MIN_CARRY_OVER)
        {
            if (total_factor < MIN_POSITIVE)
                return MIN_NEGATIVE;
            
            //lh_cost += log(total_factor);
            //total_factor = 1.0;
            total_factor *= MAX_POSITIVE;
            lh_cost -= LOG_MAX_POSITIVE;
        }
        
        // update pos
        pos = end_pos + 1;
    }
    
    return lh_cost + log(total_factor);
}

void calculateSampleCost_R_R(const SeqRegion& seq1_region, const RealNumType* const &cumulative_rate, const RealNumType blength, const PositionType pos, const PositionType end_pos, RealNumType& lh_cost)
{
    if (seq1_region.plength_observation2node < 0 && seq1_region.plength_observation2root < 0)
        lh_cost += blength * (cumulative_rate[end_pos + 1] - cumulative_rate[pos]);
    else
    {
        RealNumType total_blength = blength + seq1_region.plength_observation2node;
        if (seq1_region.plength_observation2root < 0)
            lh_cost += total_blength * (cumulative_rate[end_pos + 1] - cumulative_rate[pos]);
        else
            // here contribution from root frequency gets added and subtracted so it's ignored
            lh_cost += (total_blength + seq1_region.plength_observation2root) * (cumulative_rate[end_pos + 1] - cumulative_rate[pos]);
    }
}

template <const StateType num_states>
void calculateSampleCost_R_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType blength, const StateType seq1_state, RealNumType& lh_cost, RealNumType& total_factor, const Model& model)
{
    if (seq1_region.plength_observation2root >= 0)
    {
        RealNumType total_blength = seq1_region.plength_observation2root + blength;
        
        if (seq2_region.getLH(seq1_state) > 0.1)
        {
            total_blength += seq1_region.plength_observation2node;
            
            // here contribution from root frequency can also be also ignored
            lh_cost += model.diagonal_mut_mat[seq1_state] * total_blength;
        }
        else
        {
            RealNumType tot = 0;
            RealNumType* freq_j_transposed_ij_row = model.freq_j_transposed_ij + model.row_index[seq1_state];
            RealNumType* mutation_mat_row = model.mutation_mat;
                                
            for (StateType i = 0; i < num_states; ++i, mutation_mat_row += num_states)
            {
                RealNumType tot2 = freq_j_transposed_ij_row[i] * seq1_region.plength_observation2node + ((seq1_state == i) ? model.root_freqs[i] : 0);
                RealNumType tot3 = ((seq2_region.getLH(i) > 0.1) ? 1 : 0) + sumMutationByLh<num_states>(&(*seq2_region.likelihood)[0], mutation_mat_row);
                
                tot += tot2 * tot3 * total_blength;
            }
            
            total_factor *= tot * model.inverse_root_freqs[seq1_state];
        }
    }
    else
    {
        if (seq2_region.getLH(seq1_state) > 0.1)
        {
            if (seq1_region.plength_observation2node >= 0)
                lh_cost += model.diagonal_mut_mat[seq1_state] * (blength + seq1_region.plength_observation2node);
            else
                lh_cost += model.diagonal_mut_mat[seq1_state] * blength;
        }
        else
        {
            RealNumType tot = 0;
            RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
            
            tot += sumMutationByLh<num_states>(&(*seq2_region.likelihood)[0], mutation_mat_row);
            
            if (seq1_region.plength_observation2node >= 0)
                total_factor *= tot * (blength + seq1_region.plength_observation2node);
            else
                total_factor *= tot * blength;
        }
    }
}

void calculateSampleCost_R_ACGT(const SeqRegion& seq1_region, const RealNumType blength, const StateType seq1_state, const StateType seq2_state, RealNumType& total_factor, const Model& model)
{
    if (seq1_region.plength_observation2root >= 0)
    {
        // TODO: can cache model.mutation_mat[model.row_index[seq1_state] * model.diagonal_mut_mat[seq1_state]
        // TODO: can cache  model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * model.diagonal_mut_mat[seq2_state]
        RealNumType seq1_state_evolves_seq2_state = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * blength * (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node);
        
        RealNumType seq2_state_evolves_seq1_state = model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node * (1.0 + model.diagonal_mut_mat[seq2_state] * (blength + seq1_region.plength_observation2root));
                                                                                                                                                                                    
        total_factor *= seq1_state_evolves_seq2_state + seq2_state_evolves_seq1_state;
    }
    else
    {
        total_factor *= model.mutation_mat[model.row_index[seq1_state] + seq2_state] * (blength + (seq1_region.plength_observation2node < 0 ? 0 : seq1_region.plength_observation2node));
    }
}

template <const StateType num_states>
void calculateSampleCost_O_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType blength, RealNumType& total_factor, const Model& model)
{
    RealNumType blength13 = blength;
    if (seq1_region.plength_observation2node >= 0)
    {
        blength13 = seq1_region.plength_observation2node;
        if (blength > 0)
            blength13 += blength;
    }
    
    RealNumType tot = 0;
    
    RealNumType* mutation_mat_row = model.mutation_mat;
                        
    for (StateType i = 0; i < num_states; ++i, mutation_mat_row += num_states)
    {
        RealNumType tot2 = blength13 * sumMutationByLh<num_states>(&(*seq2_region.likelihood)[0], mutation_mat_row);
        
        tot += (tot2 + (seq2_region.getLH(i) > 0.1 ? 1 : 0)) * seq1_region.getLH(i);
    }
        
    total_factor *= tot;
}

template <const StateType num_states>
void calculateSampleCost_O_RACGT(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType blength, const PositionType end_pos, RealNumType& total_factor, const Alignment& aln, const Model& model)
{
    RealNumType blength13 = blength;
    if (seq1_region.plength_observation2node >= 0)
    {
        blength13 = seq1_region.plength_observation2node;
        if (blength > 0)
            blength13 += blength;
    }
    
    StateType seq2_state = seq2_region.type;
    if (seq2_state == TYPE_R)
        seq2_state = aln.ref_seq[end_pos];
    
    RealNumType *transposed_mut_mat_row = model.transposed_mut_mat + model.row_index[seq2_state];
    RealNumType tot2 = dotProduct<num_states>(transposed_mut_mat_row, &((*seq1_region.likelihood)[0]));
    total_factor *= seq1_region.getLH(seq2_state) + blength13 * tot2;
}

void calculateSampleCost_identicalACGT(const SeqRegion& seq1_region, const RealNumType blength, RealNumType& lh_cost, const Model& model)
{
    RealNumType total_blength = blength;
    total_blength += (seq1_region.plength_observation2node < 0 ? 0 : seq1_region.plength_observation2node);
    total_blength += (seq1_region.plength_observation2root < 0 ? 0 : seq1_region.plength_observation2root);

    lh_cost += model.diagonal_mut_mat[seq1_region.type] * total_blength;
}

template <const StateType num_states>
void calculateSampleCost_ACGT_O(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType blength, RealNumType& lh_cost, RealNumType& total_factor, const Model& model)
{
    StateType seq1_state = seq1_region.type;
    RealNumType tot = 0.0;
    
    if (seq1_region.plength_observation2root >= 0)
    {
        RealNumType blength15 = blength + seq1_region.plength_observation2root;
        
        if (seq2_region.getLH(seq1_state) > 0.1)
            lh_cost += model.diagonal_mut_mat[seq1_state] * (blength15 + seq1_region.plength_observation2node);
        else
        {
            RealNumType* freq_j_transposed_ij_row = model.freq_j_transposed_ij + model.row_index[seq1_state];
            RealNumType* mutation_mat_row = model.mutation_mat;
                                
            for (StateType i = 0; i < num_states; ++i, mutation_mat_row += num_states)
            {
                RealNumType tot2 = freq_j_transposed_ij_row[i] * seq1_region.plength_observation2node + ((seq1_state == i) ? model.root_freqs[i] : 0);
                    
                RealNumType tot3 = sumMutationByLh<num_states>(&(*seq2_region.likelihood)[0], mutation_mat_row);
                
                tot += tot2 * blength15 * tot3 + (seq2_region.getLH(i) > 0.1 ? tot2 : 0);
            }
            
            total_factor *= (tot * model.inverse_root_freqs[seq1_state]);
        }
    }
    else
    {
        RealNumType tmp_blength = blength + (seq1_region.plength_observation2node < 0 ? 0 : seq1_region.plength_observation2node);
        if (seq2_region.getLH(seq1_state) > 0.1)
            lh_cost += model.diagonal_mut_mat[seq1_state] * tmp_blength;
        else
        {
            RealNumType* mutation_mat_row = model.mutation_mat + model.row_index[seq1_state];
            tot += sumMutationByLh<num_states>(&(*seq2_region.likelihood)[0], mutation_mat_row);
            
            total_factor *= tot * tmp_blength;
        }
    }
}

void calculateSampleCost_ACGT_RACGT(const SeqRegion& seq1_region, const SeqRegion& seq2_region, const RealNumType blength, const PositionType end_pos, RealNumType& total_factor, const Alignment& aln, const Model& model)
{
    StateType seq1_state = seq1_region.type;
    StateType seq2_state = seq2_region.type;
    if (seq2_state == TYPE_R)
        seq2_state = aln.ref_seq[end_pos];
    
    if (seq1_region.plength_observation2root >= 0)
    {
        // here we ignore contribution of non-parsimonious mutational histories
        RealNumType seq1_state_evoloves_seq2_state = model.mutation_mat[model.row_index[seq1_state] + seq2_state] * (blength + seq1_region.plength_observation2root) * (1.0 + model.diagonal_mut_mat[seq1_state] * seq1_region.plength_observation2node);
        
        RealNumType seq2_state_evolves_seq1_state = model.freqi_freqj_qij[model.row_index[seq2_state] + seq1_state] * seq1_region.plength_observation2node * (1.0 + model.diagonal_mut_mat[seq2_state] * (blength + seq1_region.plength_observation2root));
        
        total_factor *= (seq1_state_evoloves_seq2_state + seq2_state_evolves_seq1_state);
    }
    else
    {
        RealNumType tmp_blength = ((seq1_region.plength_observation2node < 0) ? blength : blength + seq1_region.plength_observation2node);
        
        total_factor *= model.mutation_mat[model.row_index[seq1_state] + seq2_state] * tmp_blength;
    }
}

RealNumType Tree::calculateSamplePlacementCost(const SeqRegions* const parent_regions, const SeqRegions* const child_regions, RealNumType blength)
{
    return (this->*calculateSamplePlacementCostPointer)(parent_regions, child_regions, blength);
}

// this implementation derives from appendProb
template <const StateType num_states>
RealNumType Tree::calculateSamplePlacementCostTemplate(const SeqRegions* const parent_regions, const SeqRegions* const child_regions, RealNumType blength)
{     // 10% of total runtime
    // init dummy variables
    RealNumType lh_cost = 0;
    PositionType pos = 0;
    RealNumType total_factor = 1;
    const SeqRegions& seq1_regions = *parent_regions;
    const SeqRegions& seq2_regions = *child_regions;
    size_t iseq1 = 0;
    size_t iseq2 = 0;
    if (blength < 0) blength = 0;
    const PositionType seq_length = aln.ref_seq.size();
    
    while (pos < seq_length)
    {
        PositionType end_pos;
        RealNumType total_blength = blength;
        
        // get the next shared segment in the two sequences
        SeqRegions::getNextSharedSegment(pos, seq1_regions, seq2_regions, iseq1, iseq2, end_pos);
        const auto* seq1_region = &seq1_regions[iseq1];
        const auto* seq2_region = &seq2_regions[iseq2]; 
        
        // 1. e1.type = N || e2.type = N
        if ((seq2_region->type == TYPE_N) + (seq1_region->type == TYPE_N))
        {
            pos = end_pos + 1;
            continue;
        }
        
        // e1.type != N && e2.type != N
        // A,C,G,T
        // R -> same as the reference
        // N -> gaps
        // O -> vector of 4 probability to observe A, C, G, T
        const DoubleState s1s2 = (DoubleState(seq1_region->type) << 8) | seq2_region->type;
        
        // 2. e1.type = R
        // 2.1. e1.type = R and e2.type = R
        if (s1s2 == RR) [[likely]]
        {
            calculateSampleCost_R_R(*seq1_region, model.cumulative_rate, blength, pos, end_pos, lh_cost);
        }
        // 2.2. e1.type = R and e2.type = O
        else if (s1s2 == RO)
        {
            calculateSampleCost_R_O<num_states>(*seq1_region, *seq2_region, blength, aln.ref_seq[end_pos], lh_cost, total_factor, model);
        }
        // 2.3. e1.type = R and e2.type = A/C/G/T
        else if (seq1_region->type == TYPE_R)
        {
            calculateSampleCost_R_ACGT(*seq1_region, blength, aln.ref_seq[end_pos], seq2_region->type, total_factor, model);
        }
        // 3. e1.type = O
        // 3.1. e1.type = O and e2.type = O
        else if (s1s2 == OO)
        {
            calculateSampleCost_O_O<4>(*seq1_region, *seq2_region, blength, total_factor, model);
        }
        // 3.2. e1.type = O and e2.type = R or A/C/G/T
        else if (seq1_region->type == TYPE_O)
        {
            calculateSampleCost_O_RACGT<num_states>(*seq1_region, *seq2_region, blength, end_pos, total_factor, aln, model);
        }
        // 4. e1.type = A/C/G/T
        // 4.1. e1.type =  e2.type
        else if (seq1_region->type == seq2_region->type)
        {
            calculateSampleCost_identicalACGT(*seq1_region, blength, lh_cost, model);
        }
        // e1.type = A/C/G/T and e2.type = O/A/C/G/T
        // 4.2. e1.type = A/C/G/T and e2.type = O
        else if (seq2_region->type == TYPE_O)
        {
            calculateSampleCost_ACGT_O<4>(*seq1_region, *seq2_region, blength, lh_cost, total_factor, model);
        }
        // 4.3. e1.type = A/C/G/T and e2.type = R or A/C/G/T
        else
        {
            calculateSampleCost_ACGT_RACGT(*seq1_region, *seq2_region, blength, end_pos, total_factor, aln, model);
        }
         
        // avoid underflow on total_factor
        // approximately update lh_cost and total_factor
        if (total_factor <= MIN_CARRY_OVER)
        {
            if (total_factor < MIN_POSITIVE)
                return MIN_NEGATIVE;
            
            //lh_cost += log(total_factor);
            //total_factor = 1.0;
            total_factor *= MAX_POSITIVE;
            lh_cost -= LOG_MAX_POSITIVE;
        }
        
        // update pos
        pos = end_pos + 1;
    }
    
    return lh_cost + log(total_factor);
}

void Tree::updateZeroBlength(Node* node, stack<Node*> &node_stack, RealNumType threshold_prob)
{
    // get the top node in the phylo-node
    Node* top_node = node->getTopNode();
    ASSERT(top_node);
    SeqRegions* upper_left_right_regions = top_node->neighbor->getPartialLhAtNode(aln, model, threshold_prob);
    SeqRegions* lower_regions = top_node->getPartialLhAtNode(aln, model, threshold_prob);
    
    RealNumType best_lh = calculateSamplePlacementCost(upper_left_right_regions, lower_regions, default_blength);
    RealNumType best_length = default_blength;
    
    // try shorter lengths
    bool found_new_best_length = tryShorterNewBranch<&Tree::calculateSamplePlacementCost>(upper_left_right_regions, lower_regions, best_length, best_lh, min_blength);
    
    // try longer lengths
    if (!found_new_best_length)
        tryLongerNewBranch<&Tree::calculateSamplePlacementCost>(upper_left_right_regions, lower_regions, best_length, best_lh, max_blength);
    
    // update best_length
    top_node->length = best_length;
    top_node->neighbor->length = best_length;
    
    // add current node and its parent to node_stack to for updating partials further from these nodes
    top_node->outdated = true;
    top_node->neighbor->getTopNode()->outdated = true;
    node_stack.push(top_node);
    node_stack.push(top_node->neighbor);
}
