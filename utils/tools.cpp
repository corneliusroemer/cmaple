/***************************************************************************
 *   Copyright (C) 2022 by                                            *
 *   BUI Quang Minh <m.bui@anu.edu.au>                                *
 *   Nhan Ly-Trong <trongnhan.uit@gmail.com>                                    *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/



#include "tools.h"
#include "timeutil.h"

//#include <filesystem>

using namespace std;

VerboseMode verbose_mode;

void printCopyright(ostream &out) {
     out << "CMAPLE";
}

/**
        Output an error to screen, then exit program
        @param error error message
 */
void outError(const char *error, bool quit) {
	if (error == ERR_NO_MEMORY) {
        //print_stacktrace(cerr);
	}
	cerr << error << endl;
    if (quit)
    	exit(2);
}

/**
        Output an error to screen, then exit program
        @param error error message
 */
void outError(const string &error, bool quit) {
    outError(error.c_str(), quit);
}

void outError(const char *error, const char *msg, bool quit) {
    string str = error;
    str += msg;
    outError(str, quit);
}

void outError(const char *error, const string &msg, bool quit) {
    string str = error;
    str += msg;
    outError(str, quit);
}

/**
        Output a warning message to screen
        @param error warning message
 */
void outWarning(const char *warn) {
    cout << "WARNING: " << warn << endl;
}

void outWarning(const string &warn) {
    outWarning(warn.c_str());
}

std::istream& safeGetline(std::istream& is, std::string& t)
{
    t.clear();

    // The characters in the stream are read one-by-one using a std::streambuf.
    // That is faster than reading them one-by-one using the std::istream.
    // Code that uses streambuf this way must be guarded by a sentry object.
    // The sentry object performs various tasks,
    // such as thread synchronization and updating the stream state.

    std::istream::sentry se(is, true);
    std::streambuf* sb = is.rdbuf();

    for(;;) {
        int c = sb->sbumpc();
        switch (c) {
        case '\n':
            return is;
        case '\r':
            if(sb->sgetc() == '\n')
                sb->sbumpc();
            return is;
        case EOF:
            // Also handle the case when the last line has no line ending
            if(t.empty())
                is.setstate(std::ios::eofbit);
            return is;
        default:
            t += (char)c;
        }
    }
}

//From Tung
string convertPosTypeToString(PositionType number) {
    stringstream ss; //create a stringstream
    ss << number; //add number to the stream
    return ss.str(); //return a string with the contents of the stream
}

string convertIntToString(int number) {
    stringstream ss; //create a stringstream
    ss << number; //add number to the stream
    return ss.str(); //return a string with the contents of the stream
}

string convertInt64ToString(int64_t number) {
    stringstream ss; //create a stringstream
    ss << number; //add number to the stream
    return ss.str(); //return a string with the contents of the stream
}

string convertDoubleToString(RealNumType number) {
    stringstream ss; //create a stringstream
    ss << number; //add number to the stream
    return ss.str(); //return a string with the contents of the stream
}

bool iEquals(const string &a, const string &b)
{
    unsigned int sz = a.size();
    if (b.size() != sz)
        return false;
    for (unsigned int i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

//From Tung

bool copyFile(const char SRC[], const char DEST[]) {
    std::ifstream src; // the source file
    std::ofstream dest; // the destination file

    src.open(SRC, std::ios::binary); // open in binary to prevent jargon at the end of the buffer
    dest.open(DEST, std::ios::binary); // same again, binary
    if (!src.is_open() || !dest.is_open())
        return false; // could not be copied

    dest << src.rdbuf(); // copy the content
    dest.close(); // close destination file
    src.close(); // close source file

    return true; // file copied successfully
}

bool fileExists(const string &strFilename) {
    struct stat stFileInfo;
    bool blnReturn;
    int intStat;

    // Attempt to get the file attributes
    intStat = stat(strFilename.c_str(), &stFileInfo);
    if (intStat == 0) {
        // We were able to get the file attributes
        // so the file obviously exists.
        blnReturn = true;
    } else {
        // We were not able to get the file attributes.
        // This may mean that we don't have permission to
        // access the folder which contains this file. If you
        // need to do that level of checking, lookup the
        // return values of stat which will give you
        // more details on why stat failed.
        blnReturn = false;
    }
    return (blnReturn);
}

/*int isDirectory(const char *path) {
  return std::filesystem::is_directory(path);
}

int isFile(const char *path) {
  return std::filesystem::is_regular_file(path);
}*/

int convert_int(const char *str, int &end_pos) {
	char *endptr;
	int i = strtol(str, &endptr, 10);

	if ((i == 0 && endptr == str) || abs(i) == HUGE_VALL) {
		string err = "Expecting integer, but found \"";
		err += str;
		err += "\" instead";
		outError(err);
	}
	end_pos = endptr - str;
	return i;
}

int convert_int(const char *str) {
    char *endptr;
    int i = strtol(str, &endptr, 10);

    if ((i == 0 && endptr == str) || abs(i) == HUGE_VALL || *endptr != 0) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }

    return i;
}

PositionType convert_positiontype(const char *str) {
    char *endptr;
    PositionType i = (PositionType) strtol(str, &endptr, 10);

    if ((i == 0 && endptr == str) || i == HUGE_VALL || *endptr != 0) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }

    return i;
}

void convert_int_vec(const char *str, IntVector &vec) {
    char *beginptr = (char*)str, *endptr;
    vec.clear();
    do {
		int i = strtol(beginptr, &endptr, 10);

		if ((i == 0 && endptr == beginptr) || abs(i) == HUGE_VALL) {
			string err = "Expecting integer, but found \"";
			err += beginptr;
			err += "\" instead";
            outError(err);
		}
		vec.push_back(i);
		if (*endptr == ',') endptr++;
		beginptr = endptr;
    } while (*endptr != 0);
}


int64_t convert_int64(const char *str) {
    char *endptr;
    int64_t i = (int64_t)strtoll(str, &endptr, 10); // casted because 'long long' may be larger than int64_t

    if ((i == 0 && endptr == str) || abs(i) == HUGE_VALL || *endptr != 0) {
        string err = "Expecting large integer , but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }

    return i;
}

int64_t convert_int64(const char *str, int &end_pos) {
	char *endptr;
	int64_t i = (int64_t)strtoll(str, &endptr, 10); // casted because 'long long' may be larger than int64_t

	if ((i == 0 && endptr == str) || abs(i) == HUGE_VALL) {
		string err = "Expecting large integer, but found \"";
		err += str;
		err += "\" instead";
        outError(err);
	}
	end_pos = endptr - str;
	return i;
}


RealNumType convert_real_number(const char *str) {
    char *endptr;
    RealNumType d = strtod(str, &endptr);
    if ((d == 0.0 && endptr == str) || fabs(d) == HUGE_VALF || *endptr != 0) {
        string err = "Expecting floating-point number, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }
    return d;
}

RealNumType convert_real_number(const char *str, int &end_pos) {
	char *endptr;
	RealNumType d = strtod(str, &endptr);
	if ((d == 0.0 && endptr == str) || fabs(d) == HUGE_VALF) {
		string err = "Expecting floating-point number, but found \"";
		err += str;
		err += "\" instead";
        outError(err);
	}
	end_pos = endptr - str;
	return d;
}

void convert_real_numbers(RealNumType* &arr, string input_str)
{
    // count the number of input real_numbers
    int number_count = count(input_str.begin(), input_str.end(), ' ') + 1;
    
    // init array
    arr = new RealNumType[number_count];
    
    // parse rates
    stringstream ss(input_str);
    int index = 0;
    while (ss.good())
    {
        ss >> arr[index];
        ++index;
    }
}

void convert_real_number_vec(const char *str, RealNumberVector &vec, char separator) {
    char *beginptr = (char*)str, *endptr;
    vec.clear();
    do {
		RealNumType d = strtod(beginptr, &endptr);

		if ((d == 0.0 && endptr == beginptr) || fabs(d) == HUGE_VALF) {
			string err = "Expecting floating-point number, but found \"";
			err += beginptr;
			err += "\" instead";
            outError(err);
		}
		vec.push_back(d);
		if (*endptr == separator) endptr++;
		beginptr = endptr;
    } while (*endptr != 0);
}

string convert_time(const RealNumType sec) {
    int sec_int = (int) floor(sec);
    int secs = sec_int % 60;
    int mins = (sec_int % 3600) / 60;
    int hours = sec_int / 3600;
    stringstream ss;
    ss << hours << "h:" << mins << "m:" << secs << "s";
    return ss.str();
}

void convert_range(const char *str, int &lower, int &upper, int &step_size) {
    char *endptr;

    // parse the lower bound of the range
    int d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL || (*endptr != 0 && *endptr != ':')) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }
    //lower = d;
    int d_save = d;
    upper = d;
    if (*endptr == 0) return;


    // parse the upper bound of the range
    str = endptr + 1;
    d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL || (*endptr != 0 && *endptr != ':')) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }

    lower = d_save;
    upper = d;
    if (*endptr == 0) return;

    // parse the step size of the range
    str = endptr + 1;
    d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL || *endptr != 0) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }
    step_size = d;
}

void convert_range(const char *str, RealNumType &lower, RealNumType &upper, RealNumType &step_size) {
    char *endptr;

    // parse the lower bound of the range
    RealNumType d = strtod(str, &endptr);
    if ((d == 0.0 && endptr == str) || fabs(d) == HUGE_VALF || (*endptr != 0 && *endptr != ':')) {
        string err = "Expecting floating-point number, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }
    //lower = d;
    RealNumType d_save = d;
    upper = d;
    if (*endptr == 0) return;


    // parse the upper bound of the range
    str = endptr + 1;
    d = strtod(str, &endptr);
    if ((d == 0.0 && endptr == str) || fabs(d) == HUGE_VALF || (*endptr != 0 && *endptr != ':')) {
        string err = "Expecting floating-point number, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }

    lower = d_save;
    upper = d;
    if (*endptr == 0) return;

    // parse the step size of the range
    str = endptr + 1;
    d = strtod(str, &endptr);
    if ((d == 0.0 && endptr == str) || fabs(d) == HUGE_VALF || *endptr != 0) {
        string err = "Expecting floating-point number, but found \"";
        err += str;
        err += "\" instead";
        outError(err);
    }
    step_size = d;
}

void reinitDoubleArr(RealNumType* &arr, StateType size, bool delete_first, bool set_zero)
{
    // delete the current array
    if (delete_first && arr)
        delete [] arr;
    
    // request memory allocation for the new array
    arr = new RealNumType[size];
    if (set_zero)
        for (StateType i = 0; i < size; ++i)
            arr[i] = 0;
}

void convert_string_vec(const char *str, StrVector &vec, char separator) {
    char *beginptr = (char*)str, *endptr;
    vec.clear();
    string elem;
    do {
    	endptr = strchr(beginptr, separator);
    	if (!endptr) {
    		elem.assign(beginptr);
    		vec.push_back(elem);
    		return;
    	}
    	elem.assign(beginptr, endptr-beginptr);
    	vec.push_back(elem);
		beginptr = endptr+1;
    } while (*endptr != 0);

}

void normalize_frequencies_from_index(RealNumType* freqs, int num_states, int starting_index)
{
    ASSERT(num_states > 0);
    // calculate the total_freqs
    RealNumType total_freqs = 0;
    for (int i = starting_index; i < starting_index+num_states; ++i)
        total_freqs += freqs[i];
    
    // normalize the freqs
    if (fabs(total_freqs) < 1e-5)
        outError("Sum of state frequencies must be greater than zero!");
    total_freqs = 1.0 / total_freqs;
    for (int i = starting_index; i < starting_index+num_states; ++i)
        freqs[i] *= total_freqs;
}

bool is_number(const std::string& s)
{
    char* end = nullptr;
    double val = strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0' && val != HUGE_VAL;
}

void quickStartGuide();

void initDefaultValue(Params &params)
{
    params.aln_path = NULL;
    params.diff_path = NULL;
    params.ref_path = NULL;
    params.only_extract_diff = false;
    params.hamming_weight = 1000;
    params.model_name = "GTR";
    params.redo_inference = false;
    params.threshold_prob = 1e-8;
    params.mutation_update_period = 25;
    params.failure_limit_sample = 5;
    params.failure_limit_subtree = 4;
    params.failure_limit_subtree_short_search = 1;
    params.strict_stop_seeking_placement_sample = false;
    params.strict_stop_seeking_placement_subtree = false;
    params.strict_stop_seeking_placement_subtree_short_search = true;
    params.thresh_log_lh_sample = 200;
    params.thresh_log_lh_subtree = 160;
    params.thresh_log_lh_subtree_short_search = 40;
    params.thresh_log_lh_failure = 0.01;
    params.min_blength_factor = 0.2;
    params.min_blength_mid_factor = 4.1;
    params.max_blength_factor = 40;
    params.thresh_diff_update = 1e-7;
    params.thresh_diff_fold_update = 1.001;
    params.output_aln = NULL;
    params.num_tree_improvement = 1;
    params.thresh_entire_tree_improvement = 1;
    params.thresh_placement_cost = -1e-5;
    params.thresh_placement_cost_short_search = -1;
    params.export_binary_tree = true;
    params.optimize_branch_length = true;
    params.short_range_topo_search = false;
    params.output_testing = NULL;
}

void parseArg(int argc, char *argv[], Params &params) {
    // init parameters
    initDefaultValue(params);
    
    for (int cnt = 1; cnt < argc; ++cnt) {
        try {
            if (strcmp(argv[cnt], "--aln") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --aln <ALIGNMENT_PATH>");
                
                params.aln_path = argv[cnt];

                continue;
            }
            if (strcmp(argv[cnt], "--diff") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --diff <DIFF_PATH>");
                
                params.diff_path = argv[cnt];

                continue;
            }
            if (strcmp(argv[cnt], "--output-aln") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --output-aln <ALIGNMENT_PATH>");
                
                params.output_aln = argv[cnt];

                continue;
            }
            if (strcmp(argv[cnt], "--ref") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --ref <REF_PATH>");
                
                params.ref_path = argv[cnt];

                continue;
            }
            if (strcmp(argv[cnt], "--extract-diff") == 0) {
                
                params.only_extract_diff = true;

                continue;
            }
            if (strcmp(argv[cnt], "--hamming-weight") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --hamming-weight <WEIGHT>");
                
                params.hamming_weight = convert_real_number(argv[cnt]);
                
                if (params.hamming_weight < 0)
                    outError("<WEIGHT> must not be negative!");

                continue;
            }
            if (strcmp(argv[cnt], "--model") == 0 || strcmp(argv[cnt], "-m") == 0) {
                ++cnt;
                if (cnt >= argc)
                    outError("Use --model <model_name>");
                
                params.model_name = argv[cnt];
                continue;
            }
            if (strcmp(argv[cnt], "-redo") == 0 || strcmp(argv[cnt], "--redo") == 0) {
                params.redo_inference = true;
                continue;
            }
            if (strcmp(argv[cnt], "--thresh-prob") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --thresh-prob <PROB_THRESH>");
                
                params.threshold_prob = convert_real_number(argv[cnt]);
                
                if (params.threshold_prob <= 0)
                    outError("<PROB_THRESH> must be positive!");

                continue;
            }
            if (strcmp(argv[cnt], "--mutation-update") == 0) {
                
                ++cnt;
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --mutation-update <NUMBER>");
                
                params.mutation_update_period = convert_int(argv[cnt]);
                
                if (params.mutation_update_period <= 0)
                    outError("<NUMBER> must be positive!");

                continue;
            }
            if (strcmp(argv[cnt], "--failure-limit") == 0) {
                
                ++cnt;
                
                params.failure_limit_sample = convert_int(argv[cnt]);
                
                if (params.failure_limit_sample <= 0)
                    outError("<NUMBER> must be positive!");

                continue;
            }
            if (strcmp(argv[cnt], "--failure-limit-subtree") == 0) {
                
                ++cnt;
                
                params.failure_limit_subtree = convert_int(argv[cnt]);
                
                if (params.failure_limit_subtree <= 0)
                    outError("<NUMBER> must be positive!");

                continue;
            }
            if (strcmp(argv[cnt], "--strict-stop-init") == 0) {
                
                params.strict_stop_seeking_placement_sample = true;

                continue;
            }
            if (strcmp(argv[cnt], "--unstrict-stop-subtree") == 0) {
                
                params.strict_stop_seeking_placement_subtree = false;

                continue;
            }
            if (strcmp(argv[cnt], "--multifurcating-tree") == 0) {
                
                params.export_binary_tree = false;

                continue;
            }
            if (strcmp(argv[cnt], "--no-optimize-blength") == 0) {
                
                params.optimize_branch_length = false;

                continue;
            }
            if (strcmp(argv[cnt], "--short-topo-search") == 0) {
                
                params.short_range_topo_search = true;

                continue;
            }
            if (strcmp(argv[cnt], "--output-testing") == 0) {
                
                ++cnt;
                
                if (cnt >= argc || argv[cnt][0] == '-')
                    outError("Use --output-testing <FILE_PATH>");
                
                params.output_testing = argv[cnt];

                continue;
            }
            
            // return invalid option
            string err = "Invalid \"";
            err += argv[cnt];
            err += "\" option.";
            outError(err);
        }
        // try
        catch (const char *str) {
                exit(EXIT_SUCCESS);
        } catch (string str) {
                exit(EXIT_SUCCESS);
        } catch (...) {
            string err = "Unknown argument \"";
            err += argv[cnt];
            err += "\"";
            exit(EXIT_SUCCESS);
        }

    }
    
    // validate options
    if (!params.diff_path && !params.aln_path)
        outError("Please supply either an alignment or a Diff file to start!");
        
    if (params.only_extract_diff && !params.aln_path)
        outError("Please supply an input alignment via --aln <ALIGNMENT_PATH>");
    
    if (argc <= 1) {
        quickStartGuide();
    }
}

void quickStartGuide() {
    printCopyright(cout);
    cout << "Quick Start Guide" << endl;
    exit(0);
}

InputType detectInputFile(const char *input_file) {

    if (!fileExists(input_file))
        outError("File not found ", input_file);

    try {
        ifstream in;
        in.exceptions(ios::failbit | ios::badbit);
        in.open(input_file);

        unsigned char ch = ' ';
        unsigned char ch2 = ' ';
        int count = 0;
        do {
            in >> ch;
        } while (ch <= 32 && !in.eof() && count++ < 20);
        in >> ch2;
        in.close();
        switch (ch) {
            case '#': return IN_NEXUS;
            case '(': return IN_NEWICK;
            case '[': return IN_NEWICK;
            case '>': return IN_FASTA;
            case 'C': if (ch2 == 'L') return IN_CLUSTAL;
                      else if (ch2 == 'O') return IN_COUNTS;
                      else return IN_OTHER;
            case '!': if (ch2 == '!') return IN_MSF; else return IN_OTHER;
            default:
                if (isdigit(ch)) return IN_PHYLIP;
                return IN_OTHER;
        }
    } catch (ios::failure const&) {
        outError("Cannot read file ", input_file);
    } catch (...) {
        outError("Cannot read file ", input_file);
    }
    return IN_OTHER;
}

bool overwriteFile(char *filename) {
    ifstream infile(filename);
    if (infile.is_open()) {
        cout << "Overwrite " << filename << " (y/n)? ";
        char ch;
        cin >> ch;
        if (ch != 'Y' && ch != 'y') {
            infile.close();
            return false;
        }
    }
    infile.close();
    return true;
}

void trimString(string &str) {
    str.erase(0, str.find_first_not_of(" \n\r\t"));
    str.erase(str.find_last_not_of(" \n\r\t")+1);
}

Params& Params::getInstance() {
    static Params instance;
    return instance;
}
