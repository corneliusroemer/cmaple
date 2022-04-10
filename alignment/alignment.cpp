//
//  alignment.cpp
//  alignment
//
//  Created by NhanLT on 31/3/2022.
//

#include "alignment.h"

char symbols_protein[] = "ARNDCQEGHILKMFPSTWYVX"; // X for unknown AA
char symbols_dna[]     = "ACGT";
char symbols_rna[]     = "ACGU";
char symbols_morph[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

Alignment::Alignment()
{
    ref_seq.resize(0);
    resize(0);
    
    // init DNA as the default sequence type
    seq_type = SEQ_DNA;
    num_states = 4;
}

Alignment::Alignment(vector<StateType> n_ref_seq, vector<Sequence*> n_sequences)
{
    ref_seq = n_ref_seq;
    for (Sequence* sequence:n_sequences)
        push_back(sequence);
    
    // init DNA as the default sequence type
    seq_type = SEQ_DNA;
    num_states = 4;
}

Alignment::~Alignment()
{
    for (iterator it = begin(); it != end(); it++)
        delete (*it);
}

void Alignment::processSeq(string &sequence, string &line, int line_num) {
    for (string::iterator it = line.begin(); it != line.end(); it++) {
        if ((*it) <= ' ') continue;
        if (isalnum(*it) || (*it) == '-' || (*it) == '?'|| (*it) == '.' || (*it) == '*' || (*it) == '~')
            sequence.append(1, toupper(*it));
        else if (*it == '(' || *it == '{') {
            auto start_it = it;
            while (*it != ')' && *it != '}' && it != line.end())
                it++;
            if (it == line.end())
                throw "Line " + convertIntToString(line_num) + ": No matching close-bracket ) or } found";
            sequence.append(1, '?');
            cout << "NOTE: Line " << line_num << ": " << line.substr(start_it-line.begin(), (it-start_it)+1) << " is treated as unknown character" << endl;
        } else {
            throw "Line " + convertIntToString(line_num) + ": Unrecognized character "  + *it;
        }
    }
}

void Alignment::readFasta(char *aln_path, StrVector &sequences, StrVector &seq_names, bool check_min_seqs){
    ostringstream err_str;
    igzstream in;
    int line_num = 1;
    string line;

    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(aln_path);
    // remove the failbit
    in.exceptions(ios::badbit);

    {
        cout << "Reading FASTA file" <<endl;
        
        for (; !in.eof(); line_num++) {
            safeGetline(in, line);
            if (line == "") {
                continue;
            }

            if (line[0] == '>') { // next sequence
                string::size_type pos = line.find_first_of("\n\r");
                seq_names.push_back(line.substr(1, pos-1));
                trimString(seq_names.back());
                sequences.push_back("");
                continue;
            }
            
            // read sequence contents
            if (sequences.empty()) {
                throw "First line must begin with '>' to define sequence name";
            }
            
            processSeq(sequences.back(), line, line_num);
        }
    }

    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();
    
    if (sequences.size() < MIN_NUM_TAXA && check_min_seqs)
        throw "There must be at least " + convertIntToString(MIN_NUM_TAXA) + " sequences";

    // now try to cut down sequence name if possible
    int i, step = 0;
    StrVector new_seq_names, remain_seq_names;
    new_seq_names.resize(seq_names.size());
    remain_seq_names = seq_names;

    double startShorten = getRealTime();
    for (step = 0; step < 4; step++) {
        bool duplicated = false;
        unordered_set<string> namesSeenThisTime;
        //Set of shorted names seen so far, this iteration
        for (i = 0; i < seq_names.size(); i++) {
            if (remain_seq_names[i].empty()) continue;
            size_t pos = remain_seq_names[i].find_first_of(" \t");
            if (pos == string::npos) {
                new_seq_names[i] += remain_seq_names[i];
                remain_seq_names[i] = "";
            } else {
                new_seq_names[i] += remain_seq_names[i].substr(0, pos);
                remain_seq_names[i] = "_" + remain_seq_names[i].substr(pos+1);
            }
            if (!duplicated) {
                //add the shortened name for sequence i to the
                //set of shortened names seen so far, and set
                //duplicated to true if it was already there.
                duplicated = !namesSeenThisTime.insert(new_seq_names[i]).second;
            }
        }
        if (!duplicated) break;
    }
    
    if (verbose_mode >= VB_MED) {
        cout.precision(6);
        cout << "Name shortening took " << (getRealTime() - startShorten) << " seconds." << endl;
    }
    
    if (step > 0) {
        for (i = 0; i < seq_names.size(); i++)
            if (seq_names[i] != new_seq_names[i]) {
                cout << "NOTE: Change sequence name '" << seq_names[i] << "' -> " << new_seq_names[i] << endl;
            }
    }

    seq_names = new_seq_names;
}

void Alignment::readPhylip(char *aln_path, StrVector &sequences, StrVector &seq_names, bool check_min_seqs)
{
    ostringstream err_str;
    igzstream in;
    int line_num = 1;
    
    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(aln_path);
    int seq_id = 0;
    int nseq = 0;
    int nsite = 0;
    string line;
    
    // remove the failbit
    in.exceptions(ios::badbit);

    for (; !in.eof(); line_num++) {
        safeGetline(in, line);
        line = line.substr(0, line.find_first_of("\n\r"));
        if (line == "") continue;

        if (nseq == 0) { // read number of sequences and sites
            istringstream line_in(line);
            if (!(line_in >> nseq >> nsite))
                throw "Invalid PHYLIP format. First line must contain number of sequences and sites";
           
            if (nseq < MIN_NUM_TAXA && check_min_seqs)
                throw "There must be at least " + convertIntToString(MIN_NUM_TAXA) + " sequences";
            if (nsite < 1)
                throw "No alignment columns";

            seq_names.resize(nseq, "");
            sequences.resize(nseq, "");

        } else { // read sequence contents
            if (seq_names[seq_id] == "") { // cut out the sequence name
                string::size_type pos = line.find_first_of(" \t");
                if (pos == string::npos) pos = 10; //  assume standard phylip
                seq_names[seq_id] = line.substr(0, pos);
                line.erase(0, pos);
            }
            
            int old_len = sequences[seq_id].length();
            processSeq(sequences[seq_id], line, line_num);
            
            if (sequences[seq_id].length() != sequences[0].length()) {
                err_str << "Line " << line_num << ": Sequence " << seq_names[seq_id] << " has wrong sequence length " << sequences[seq_id].length() << endl;
                throw err_str.str();
            }
            if (sequences[seq_id].length() > old_len)
                seq_id++;
            if (seq_id == nseq) {
                seq_id = 0;
            }
        }
    }
    
    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();
}

void Alignment::readSequences(char* aln_path, StrVector &sequences, StrVector &seq_names, bool check_min_seqs)
{
    // detect the input file format
    InputType intype = detectInputFile(aln_path);
    
    // read the input file
    cout << "Reading alignment file " << aln_path << " ... ";
    switch (intype) {
        case IN_FASTA:
            cout << "Fasta format detected" << endl;
            readFasta(aln_path, sequences, seq_names, check_min_seqs);
            break;
            
        case IN_PHYLIP:
            cout << "Phylip format detected" << endl;
            readPhylip(aln_path, sequences, seq_names, check_min_seqs);
            break;
            
        default:
            outError("Please input an alignment file in FASTA or PHYLIP format!");
            break;
    }
}

string Alignment::generateRef(StrVector sequences, StrVector seq_names, bool only_extract_diff)
{
    // validate the input sequences
    if (sequences.size() == 0 || sequences[0].length() == 0)
        outError("Empty input sequences. Please check & try again!");
    
    cout << "Generating a reference sequence from the input alignment..." << endl;
    
    // init dummy variables
    char NULL_CHAR = '\0';
    string ref_str (sequences[0].length(), NULL_CHAR);
    
    // determine a character for each site one by one
    int threshold = sequences.size()/2;
    for (int i = 0; i < ref_str.length(); i++)
    {
        // Init a map to count the number of times each character appears
        std::map<char, int> num_appear;
        
        for (int j = 0; j < sequences.size(); j++)
        {
            // update num_appear for the current character
            int count = num_appear[sequences[j][i]] + 1;
            num_appear[sequences[j][i]] = count;
            
            // stop counting if a character appear in more than 1/2 sequences at the current site
            if (count >= threshold)
            {
                ref_str[i] = sequences[j][i];
                break;
            }
        }
        
        // manually determine the most popular charater for the current site (if no character dominates all the others)
        if (ref_str[i] == NULL_CHAR)
        {
            for (const std::pair<const char, int>& character : num_appear)
                if (ref_str[i] == NULL_CHAR || character.second > num_appear[ref_str[i]])
                    ref_str[i] = character.first;
        }
    }
    
    // parse ref_sequence into vector of states
    if (!only_extract_diff)
        parseRefSeq(ref_str);
    
    // return the reference genome
    return ref_str;
}

string Alignment::readRef(char* ref_path, bool only_extract_diff)
{
    ASSERT(ref_path);
    if (!fileExists(ref_path))
        outError("File not found ", ref_path);
    
    string ref_str = "";
    
    // read sequences from file
    cout << "Reading a reference sequence from file..." << endl;
    StrVector str_sequences;
    StrVector seq_names;
    readSequences(ref_path, str_sequences, seq_names, false);
    
    // validate the input sequence(s)
    if (str_sequences.size() == 0 || str_sequences[0].length() == 0)
        outError("No sequence found for the reference!");
    
    // extract the ref_sequence
    ref_str = str_sequences[0];
    
    // parse ref_str into vector of states (if necessary)
    if (!only_extract_diff)
        parseRefSeq(ref_str);
    
    return ref_str;
}

void Alignment::outputMutation(ofstream &out, Sequence* sequence, char state_char, PositionType pos, PositionType length)
{
    // output the mutation into a Diff file
    out << state_char << "\t" << pos;
    if (length != -1)
        out << "\t" << length;
    out << endl;
    
    // add the mutation into sequence
    if (sequence)
    {
        if (length == -1)
            sequence->mutations.push_back(new Mutation(convertChar2State(state_char), pos));
        else
            sequence->mutations.push_back(new MutationIndel(convertChar2State(state_char), pos, length));
    }
}

void Alignment::extractMutations(StrVector str_sequences, StrVector seq_names, string ref_sequence, ofstream &out, bool only_extract_diff)
{
    ASSERT(str_sequences.size() == seq_names.size() && str_sequences.size() > 0 && out);
    resize(0);
    Sequence* sequence = NULL;
    
    // extract mutations of sequences one by one
    for (int i = 0; i < str_sequences.size(); i++)
    {
        // validate the sequence length
        string str_sequence = str_sequences[i];
        if (str_sequence.length() != ref_sequence.length())
            outError("The sequence length of " + seq_names[i] + " (" + convertIntToString(str_sequence.length()) + ") is different from that of the reference sequence (" + convertIntToString(ref_sequence.length()) + ")!");
        
        // write taxon name
        out << ">" << seq_names[i] << endl;
        
        // init new sequence instance for the inference process afterwards
        if (!only_extract_diff)
        {
            sequence = new Sequence(seq_names[i]);
            push_back(sequence);
        }
        
        // init dummy variables
        int state = 0;
        int length = 0;
        for (int pos = 0; pos < ref_sequence.length(); pos++)
        {
            switch (state)
            {
                case 0: // previous character is neither 'N' nor '-'
                    if (str_sequence[pos] != ref_sequence[pos])
                    {
                        length = 1;
                        
                        // starting a sequence of 'N'
                        if (toupper(str_sequence[pos]) == 'N')
                            state = 1;
                        // starting a sequence of '-'
                        else if (str_sequence[pos] == '-')
                            state = 2;
                        // output a mutation
                        else
                            outputMutation(out, sequence, str_sequence[pos], pos + 1);
                    }
                    break;
                case 1: // previous character is 'N'
                    // inscrease the length if the current character is still 'N'
                    if (toupper(str_sequence[pos]) == 'N' && str_sequence[pos] != ref_sequence[pos])
                        length++;
                    else
                    {
                        // output the previous sequence of 'N'
                        outputMutation(out, sequence, str_sequence[pos-1], pos + 1 - length, length);
                        
                        // reset state
                        state = 0;
                        
                        // handle new character different from the reference
                        if (str_sequence[pos] != ref_sequence[pos])
                        {
                            length = 1;
                            // starting a sequence of '-'
                            if (str_sequence[pos] == '-')
                                state = 2;
                            // output a mutation
                            else
                            {
                                outputMutation(out, sequence, str_sequence[pos], pos + 1);
                                state = 0;
                            }
                        }
                    }
                    break;
                case 2: // previous character is '-'
                    // inscrease the length if the current character is still '-'
                    if (toupper(str_sequence[pos]) == '-' && str_sequence[pos] != ref_sequence[pos])
                        length++;
                    else
                    {
                        // output the previous sequence of '-'
                        outputMutation(out, sequence, str_sequence[pos-1], pos + 1 - length, length);
                        
                        // reset state
                        state = 0;
                        
                        // handle new character different from the reference
                        if (str_sequence[pos] != ref_sequence[pos])
                        {
                            length = 1;
                            // starting a sequence of 'N'
                            if (toupper(str_sequence[pos]) == 'N')
                                state = 1;
                            // output a mutation
                            else
                            {
                                outputMutation(out, sequence, str_sequence[pos], pos + 1);
                                state = 0;
                            }
                        }
                    }
                    break;
            }
        }
        
        //  output the last sequence of 'N' or '-' (if any)
        if (state != 0)
            outputMutation(out, sequence, str_sequence[str_sequence.length() - 1], str_sequence.length() + 1 - length, length);
    }
}

void Alignment::parseRefSeq(string ref_sequence)
{
    ref_seq.resize(ref_sequence.length());
    for (PositionType i = 0; i < ref_sequence.length(); i++)
    {
        ref_seq[i] = convertChar2State(ref_sequence[i]);
        
        // validate the ref state
        if (ref_seq[i] >= num_states)
            outError("Invalid reference state found at site " + convertPosTypeToString(i));
    }
}

void Alignment::readDiff(char* diff_path, char* ref_path)
{
    ASSERT(diff_path);
    
    if (!fileExists(diff_path))
        outError("File not found ", diff_path);
    
    // read the reference sequence (if the user supplies it)
    if (ref_path)
        readRef(ref_path, false);
    
    // init dummy variables
    string seq_name = "";
    vector<Mutation*> regions;
    PositionType current_pos = 1;
    ifstream in = ifstream(diff_path);
    int line_num = 1;
    string line;

    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    // remove the failbit
    in.exceptions(ios::badbit);

    cout << "Reading a Diff file" << endl;
    
    // extract reference sequence first
    for (; !in.eof(); line_num++)
    {
        safeGetline(in, line);
        if (line == "") continue;
        
        // read the first line (">REF")
        if (line[0] == '>')
        {
            string::size_type pos = line.find_first_of("\n\r");
            seq_name = line.substr(1, pos-1);
            
            if (seq_name != REF_NAME)
                outError("Diff file must start by >REF. Please check and try again!");
        }
        // read the reference sequence
        else
        {
            // make sure the first line was found
            if (seq_name != REF_NAME)
                outError("Diff file must start by >REF. Please check and try again!");
            
            // choose the ref sequence if the user already supplies a reference sequence
            if (ref_path)
                outWarning("Skipping the reference sequence in the Diff file since the reference sequence is already specified via '--ref' option.");
            // otherwise, parse the reference sequence into vector of state
            else
                parseRefSeq(line);
            
            // reset the seq_name
            seq_name = "";
            
            // break to read sequences of other taxa
            break;
        }
    }
    
    // extract sequences of other taxa one by one
    for (; !in.eof(); line_num++)
    {
        safeGetline(in, line);
        if (line == "") continue;
        
        // Read sequence name
        if (line[0] == '>')
        {
            // record the sequence of the previous taxon
            if (seq_name.length() > 0)
            {
                // insert a Region type R (if necessary)
                if (current_pos <= ref_seq.size())
                    regions.push_back(new Region(TYPE_R, current_pos, seq_type, num_states));
                
                push_back(new Sequence(seq_name, regions));
                
                // reset dummy variables
                seq_name = "";
                current_pos = 1;
                regions.clear();
            }
            
            // Read new sequence name
            string::size_type pos = line.find_first_of("\n\r");
            seq_name = line.substr(1, pos-1);
            if (seq_name.length() == 0)
                outError("Empty sequence name found at line " + convertIntToString(line_num) + ". Please check and try again!");
        }
        // Read a Mutation
        else
        {
            // validate the input
            char separator = '\t';
            size_t num_items = std::count(line.begin(), line.end(), separator) + 1;
            if (num_items < 2 || num_items > 3)
                outError("Invalid input. Each difference must be presented be <Type>    <Position>  [<Length>]. Please check and try again!");
            
            // extract mutation info
            stringstream ssin(line);
            string tmp;
            
            // extract <Type>
            ssin >> tmp;
            StateType state = convertChar2State(toupper(tmp[0]));
            
            // extract <Position>
            ssin >> tmp;
            PositionType pos = convert_positiontype(tmp.c_str());
            if (pos <= 0 || pos > ref_seq.size())
                outError("<Position> must be greater than 0 and less than the reference sequence length (" + convertPosTypeToString(ref_seq.size()) + ")!");
            
            // extract <Length>
            PositionType length = 1;
            if (ssin.good())
            {
                ssin >> tmp;
                if (state == TYPE_N || state == TYPE_DEL)
                {
                    length = convert_positiontype(tmp.c_str());
                    if (length <= 0)
                        outError("<Length> must be greater than 0!");
                    if (length + pos > ref_seq.size())
                        outError("<Length> + <Position> must be less than the reference sequence length (" + convertPosTypeToString(ref_seq.size()) + ")!");
                }
                else
                    outWarning("Ignoring <Length> of " + tmp + ". <Length> is only appliable for 'N' or '-'.");
            }
            
            // insert a Region type R (if necessary)
            if (current_pos < pos)
                regions.push_back(new Region(TYPE_R, current_pos, seq_type, num_states));
            
            // add a new region into regions
            regions.push_back(new Region(state, pos, seq_type, num_states));
            
            // update current_pos
            current_pos = pos + length;
        }
    }
    
    // Record the sequence of  the last taxon
    if (seq_name.length() > 0)
    {
        // insert a Region type R (if necessary)
        if (current_pos <= ref_seq.size())
            regions.push_back(new Region(TYPE_R, current_pos, seq_type, num_states));
        
        push_back(new Sequence(seq_name, regions));
    }
    
    // validate the input
    if (ref_seq.size() == 0)
        outError("Reference sequence is not found!");
    if (size() < MIN_NUM_TAXA)
        outError("The number of taxa must be at least " + convertIntToString(MIN_NUM_TAXA));

    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();
}

char Alignment::convertState2Char(StateType state) {
    if (state == TYPE_N || state == TYPE_DEL) return '-';
    if (state > TYPE_INVALID) return '?';

    switch (seq_type) {
    case SEQ_BINARY:
        switch (state) {
        case 0:
            return '0';
        case 1:
            return '1';
        default:
            return '?'; // unrecognize character
        }
    case SEQ_DNA: // DNA
        switch (state) {
        case 0:
            return 'A';
        case 1:
            return 'C';
        case 2:
            return 'G';
        case 3:
            return 'T';
        case 1+4+3:
            return 'R'; // A or G, Purine
        case 2+8+3:
            return 'Y'; // C or T, Pyrimidine
        case 1+8+3:
            return 'W'; // A or T, Weak
        case 2+4+3:
            return 'S'; // G or C, Strong
        case 1+2+3:
            return 'M'; // A or C, Amino
        case 4+8+3:
            return 'K'; // G or T, Keto
        case 2+4+8+3:
            return 'B'; // C or G or T
        case 1+2+8+3:
            return 'H'; // A or C or T
        case 1+4+8+3:
            return 'D'; // A or G or T
        case 1+2+4+3:
            return 'V'; // A or G or C
        default:
            return '?'; // unrecognize character
        }
        return state;
    case SEQ_PROTEIN: // Protein
        if (state < 20)
            return symbols_protein[(int)state];
        else if (state == 20) return 'B';
        else if (state == 21) return 'Z';
        else if (state == 22) return 'J';
//        else if (state == 4+8+19) return 'B';
//        else if (state == 32+64+19) return 'Z';
        else
            return '-';
    case SEQ_MORPH:
        // morphological state
        if (state < strlen(symbols_morph))
            return symbols_morph[state];
        else
            return '-';
    default:
        // unknown
        return '*';
    }
}

StateType Alignment::convertChar2State(char state) {
    if (state == '-')
        return TYPE_DEL;
    if (state == '?' || state == '.' || state == '~')
        return TYPE_N;

    char *loc;

    switch (seq_type) {
    case SEQ_BINARY:
        switch (state) {
        case '0':
            return 0;
        case '1':
            return 1;
        default:
            {
                string invalid_state_msg = "Invalid state ";
                invalid_state_msg += state;
                invalid_state_msg += ". Please check and try again!";
                outError(invalid_state_msg);
                return TYPE_INVALID;
            }
                }
        break;
    case SEQ_DNA: // DNA
        switch (state) {
        case 'A':
            return 0;
        case 'C':
            return 1;
        case 'G':
            return 2;
        case 'T':
            return 3;
        case 'U':
            return 3;
        case 'R':
            return 1+4+3; // A or G, Purine
        case 'Y':
            return 2+8+3; // C or T, Pyrimidine
        case 'O':
        case 'N':
        case 'X':
            return TYPE_N;
        case 'W':
            return 1+8+3; // A or T, Weak
        case 'S':
            return 2+4+3; // G or C, Strong
        case 'M':
            return 1+2+3; // A or C, Amino
        case 'K':
            return 4+8+3; // G or T, Keto
        case 'B':
            return 2+4+8+3; // C or G or T
        case 'H':
            return 1+2+8+3; // A or C or T
        case 'D':
            return 1+4+8+3; // A or G or T
        case 'V':
            return 1+2+4+3; // A or G or C
        default:
            {
                string invalid_state_msg = "Invalid state ";
                invalid_state_msg += state;
                invalid_state_msg += ". Please check and try again!";
                outError(invalid_state_msg);
                return TYPE_INVALID; // unrecognize character
            }
        }
        return state;
    case SEQ_PROTEIN: // Protein
//        if (state == 'B') return 4+8+19;
//        if (state == 'Z') return 32+64+19;
        if (state == 'B') return 20;
        if (state == 'Z') return 21;
        if (state == 'J') return 22;
        if (state == '*') return TYPE_N; // stop codon
        if (state == 'U') return TYPE_N; // 21st amino-acid
        if (state == 'O') return TYPE_N; // 22nd amino-acid
        loc = strchr(symbols_protein, state);

        if (!loc)
        {
            string invalid_state_msg = "Invalid state ";
            invalid_state_msg += state;
            invalid_state_msg += ". Please check and try again!";
            outError(invalid_state_msg);
            return TYPE_INVALID; // unrecognize character
        }
        state = loc - symbols_protein;
        if (state < 20)
            return state;
        else
            return TYPE_N;
    case SEQ_MORPH: // Standard morphological character
        loc = strchr(symbols_morph, state);

        if (!loc)
        {
            string invalid_state_msg = "Invalid state ";
            invalid_state_msg += state;
            invalid_state_msg += ". Please check and try again!";
            outError(invalid_state_msg);
            return TYPE_INVALID; // unrecognize character
        }
        state = loc - symbols_morph;
        return state;
    default:
        {
            string invalid_state_msg = "Invalid state ";
            invalid_state_msg += state;
            invalid_state_msg += ". Please check and try again!";
            outError(invalid_state_msg);
            return TYPE_INVALID;
        }
    }
}

void Alignment::convertSequences()
{
    for (iterator it = begin(); it != end(); it++)
        (*it)->convertMutation2Region(ref_seq.size(), seq_type, num_states);
}

void Alignment::sortSeqsByDistances(double hamming_weight)
{
    ASSERT(ref_seq.size() > 0);
    
    // init dummy variables
    PositionType seq_length = ref_seq.size();
    int num_seqs = size();
    PositionType *distances = new PositionType[num_seqs];
    int *sequence_indexes = new int[num_seqs];
    
    // calculate the distance for genomelist
    for (PositionType i = 0; i < num_seqs; i++)
    {
        // dummy variables
        Sequence* sequence = at(i);
        int num_ambiguities = 0;
        int num_diffs = 0;
        sequence_indexes[i] = i;
        
        // browse regions one by one
        for (Mutation* region: sequence->mutations)
        {
            switch (region->type)
            {
                case TYPE_R: // handle sites in the sequence that are identical to the reference genome
                    break;
                case TYPE_N: // handle unsequenced sites ('-' or 'N')
                case TYPE_O: // handle ambiguity
                    num_ambiguities += region->getLength();
                    break;
                default:
                    // handle normal character
                    if (region->type < num_states)
                        num_diffs++;
                    else
                        outError("Invalid entry type!");
                    break;
            }
        }
        
        // calculate and record the distance of the current sequence
        distances[i] = num_diffs * hamming_weight + num_ambiguities;
    }

    // sort distances
    quicksort(distances, 0, num_seqs - 1, sequence_indexes);

    // re-order sequences by distances
    Sequence** tmp_sequences = new Sequence*[size()];
    memcpy(tmp_sequences, this->data(), sizeof(Sequence*)*size());
    for (int i = 0; i < num_seqs; i++)
        at(i) = tmp_sequences[sequence_indexes[i]];
    
    // delete distances, sequence_indexes
    delete[] tmp_sequences;
    delete[] distances;
    delete[] sequence_indexes;
}
