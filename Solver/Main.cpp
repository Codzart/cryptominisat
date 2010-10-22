/*****************************************************************************
MiniSat -- Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
CryptoMiniSat -- Copyright (c) 2009 Mate Soos

Original code by MiniSat authors are under an MIT licence.
Modifications for CryptoMiniSat are under GPLv3 licence.
******************************************************************************/

/**
@mainpage CryptoMiniSat
@author Mate Soos, and collaborators

CryptoMiniSat is an award-winning SAT solver based on MiniSat. It brings a
number of benefits relative to MiniSat, among them XOR clauses, extensive
failed literal probing, and better random search.

The solver basically performs the following steps:

1) parse CNF file into clause database

2) run Conflict-Driven Clause-Learning DPLL on the clauses

3) regularly run simplification passes on the clause-set

4) display solution and if not used as a library, exit

Here is a picture of of the above process in more detail:

\image html "main_flowgraph.png"

*/

#include <ctime>
#include <cstring>
#include <errno.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <omp.h>
#ifdef _MSC_VER
#include <msvc/stdint.h>
#else
#include <stdint.h>
#endif //_MSC_VER

#include <signal.h>

#ifdef STATS_NEEDED
#include "Logger.h"
#endif //STATS_NEEDED

#include "time_mem.h"
#include "constants.h"
#include "DimacsParser.h"

#if defined(__linux__)
#include <fpu_control.h>
#endif

#include "Main.h"

Main::Main(int _argc, char** _argv) :
        grouping(false)
        , debugLib (false)
        , debugNewVar (false)
        , printResult (true)
        , max_nr_of_solutions (1)
        , fileNamePresent (false)
        , twoFileNamesPresent (false)
        , argc(_argc)
        , argv(_argv)
{
}

template<class T, class T2>
void Main::printStatsLine(string left, T value, T2 value2, string extra)
{
    std::cout << std::fixed << std::left << std::setw(24) << left << ": " << std::setw(11) << std::setprecision(2) << value << " (" << std::left << std::setw(9) << std::setprecision(2) << value2 << " " << extra << ")" << std::endl;
}

template<class T>
void Main::printStatsLine(string left, T value, string extra)
{
    std::cout << std::fixed << std::left << std::setw(24) << left << ": " << std::setw(11) << std::setprecision(2) << value << extra << std::endl;
}

/**
@brief prints the statistics line at the end of solving

Prints all sorts of statistics, like number of restarts, time spent in
SatELite-type simplification, number of unit claues found, etc.
*/
void Main::printStats(Solver& solver)
{
    double   cpu_time = cpuTime();
    uint64_t mem_used = memUsed();

    //Restarts stats
    printStatsLine("c restarts", solver.starts);
    printStatsLine("c dynamic restarts", solver.dynStarts);
    printStatsLine("c static restarts", solver.staticStarts);
    printStatsLine("c full restarts", solver.fullStarts);

    //Learnts stats
    printStatsLine("c learnts DL2", solver.nbGlue2);
    printStatsLine("c learnts size 2", solver.numNewBin);
    printStatsLine("c learnts size 1", solver.get_unitary_learnts_num(), (double)solver.get_unitary_learnts_num()/(double)solver.nVars()*100.0, "% of vars");
    printStatsLine("c filedVS time", solver.getTotalTimeFailedVarSearcher(), solver.getTotalTimeFailedVarSearcher()/cpu_time*100.0, "% time");

    //Subsumer stats
    printStatsLine("c v-elim SatELite", solver.getNumElimSubsume(), (double)solver.getNumElimSubsume()/(double)solver.nVars()*100.0, "% vars");
    printStatsLine("c SatELite time", solver.getTotalTimeSubsumer(), solver.getTotalTimeSubsumer()/cpu_time*100.0, "% time");

    //XorSubsumer stats
    printStatsLine("c v-elim xor", solver.getNumElimXorSubsume(), (double)solver.getNumElimXorSubsume()/(double)solver.nVars()*100.0, "% vars");
    printStatsLine("c xor elim time", solver.getTotalTimeXorSubsumer(), solver.getTotalTimeXorSubsumer()/cpu_time*100.0, "% time");

    //VarReplacer stats
    printStatsLine("c num binary xor trees", solver.getNumXorTrees());
    printStatsLine("c binxor trees' crown", solver.getNumXorTreesCrownSize(), (double)solver.getNumXorTreesCrownSize()/(double)solver.getNumXorTrees(), "leafs/tree");

    //OTF clause improvement stats
    printStatsLine("c OTF clause improved", solver.improvedClauseNo, (double)solver.improvedClauseNo/(double)solver.conflicts, "clauses/conflict");
    printStatsLine("c OTF impr. size diff", solver.improvedClauseSize, (double)solver.improvedClauseSize/(double)solver.improvedClauseNo, " lits/clause");

    //Clause-shrinking through watchlists
    printStatsLine("c OTF cl watch-shrink", solver.numShrinkedClause, (double)solver.numShrinkedClause/(double)solver.conflicts, "clauses/conflict");
    printStatsLine("c OTF cl watch-sh-lit", solver.numShrinkedClauseLits, (double)solver.numShrinkedClauseLits/(double)solver.numShrinkedClause, " lits/clause");
    printStatsLine("c tried to recurMin cls", solver.moreRecurMinLDo, (double)solver.moreRecurMinLDo/(double)solver.conflicts*100.0, " % of conflicts");
    printStatsLine("c updated cache", solver.updateTransCache, solver.updateTransCache/(double)solver.moreRecurMinLDo, " lits/tried recurMin");

    #ifdef USE_GAUSS
    if (solver.gaussconfig.decision_until > 0) {
        std::cout << "c " << std::endl;
        printStatsLine("c gauss unit truths ", solver.get_sum_gauss_unit_truths());
        printStatsLine("c gauss called", solver.get_sum_gauss_called());
        printStatsLine("c gauss conflicts ", solver.get_sum_gauss_confl(), (double)solver.get_sum_gauss_confl() / (double)solver.get_sum_gauss_called() * 100.0, " %");
        printStatsLine("c gauss propagations ", solver.get_sum_gauss_prop(), (double)solver.get_sum_gauss_prop() / (double)solver.get_sum_gauss_called() * 100.0, " %");
        printStatsLine("c gauss useful", ((double)solver.get_sum_gauss_prop() + (double)solver.get_sum_gauss_confl())/ (double)solver.get_sum_gauss_called() * 100.0, " %");
        std::cout << "c " << std::endl;
    }
    #endif

    printStatsLine("c clauses over max glue", solver.nbClOverMaxGlue, (double)solver.nbClOverMaxGlue/(double)solver.conflicts*100.0, "% of all clauses");

    //Search stats
    printStatsLine("c conflicts", solver.conflicts, (double)solver.conflicts/cpu_time, "/ sec");
    printStatsLine("c decisions", solver.decisions, (double)solver.rnd_decisions*100.0/(double)solver.decisions, "% random");
    printStatsLine("c bogo-props", solver.propagations, (double)solver.propagations/cpu_time, "/ sec");
    printStatsLine("c conflict literals", solver.tot_literals, (double)(solver.max_literals - solver.tot_literals)*100.0/ (double)solver.max_literals, "% deleted");

    //General stats
    printStatsLine("c Memory used", (double)mem_used / 1048576.0, " MB");
    printStatsLine("c CPU time", cpu_time, " s");
}

Solver* solverToInterrupt;

/**
@brief For correctly and gracefully exiting

It can happen that the user requests a dump of the learnt clauses. In this case,
the program must wait until it gets to a state where the learnt clauses are in
a correct state, then dump these and quit normally. This interrupt hander
is used to achieve this
*/
void SIGINT_handler(int signum)
{
    Solver& solver = *solverToInterrupt;
    printf("\n");
    printf("*** INTERRUPTED ***\n");
    if (solver.conf.needToDumpLearnts || solver.conf.needToDumpOrig) {
        solver.needToInterrupt = true;
        printf("*** Please wait. We need to interrupt cleanly\n");
        printf("*** This means we might need to finish some calculations\n");
        printf("*** INTERRUPTED ***\n");
    } else {
        Main::printStats(solver);
        exit(1);
    }
}

#ifndef DISABLE_ZLIB
gzFile Main::openGzFile(int inNum)
{
    gzFile in = gzdopen(inNum, "rb");
    return in;
}

gzFile Main::openGzFile(const char* name)
{
    gzFile in = gzopen(name, "rb");
    return in;
}
#endif //DISABLE_ZLIB

template<class B>
void Main::readInAFile(B stuff, Solver& solver)
{
    if (solver.conf.verbosity >= 1) {
        if ((const char*)stuff == (const char*)fileno(stdin)) {
            std::cout << "c Reading from standard input... Use '-h' or '--help' for help." << std::endl;
        } else {
            std::cout << "c Reading file '" << stuff << "'" << std::endl;
        }
    }
    #ifdef DISABLE_ZLIB
        FILE * in = fopen(stuff, "rb");
    #else
        gzFile in = openGzFile(stuff);
    #endif // DISABLE_ZLIB

    if (in == NULL) {
        std::cout << "ERROR! Could not open file " << stuff << std::endl;
        exit(1);
    }

    DimacsParser parser(&solver, debugLib, debugNewVar, grouping);
    parser.parse_DIMACS(in);

    #ifdef DISABLE_ZLIB
        fclose(in);
    #else
        gzclose(in);
    #endif // DISABLE_ZLIB
}

void Main::parseInAllFiles(Solver& solver)
{
    double myTime = cpuTime();

    //First read normal extra files
    if ((debugLib || debugNewVar) && filesToRead.size() > 0) {
        std::cout << "debugNewVar and debugLib must both be OFF to parse in extra files" << std::endl;
        exit(-1);
    }
    for (uint32_t i = 0; i < filesToRead.size(); i++) {
        readInAFile(filesToRead[i].c_str(), solver);
    }

    //Then read the main file or standard input
    if (!fileNamePresent) {
        readInAFile(fileno(stdin), solver);
    } else {
        readInAFile(argv[(twoFileNamesPresent ? argc-2 : argc-1)], solver);
    }

    if (solver.conf.verbosity >= 1) {
        std::cout << "c Parsing time: "
        << std::fixed << std::setw(5) << std::setprecision(2) << (cpuTime() - myTime)
        << " s" << std::endl;
    }
}

//=================================================================================================
// Main:

void Main::printUsage(char** argv)
{
#ifdef DISABLE_ZLIB
    printf("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input is plain DIMACS.\n\n", argv[0]);
#else
    printf("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n\n", argv[0]);
#endif // DISABLE_ZLIB
    printf("OPTIONS:\n\n");
    printf("  --polarity-mode  = {true,false,rnd,auto} [default: auto]. Selects the default\n");
    printf("                     polarity mode. Auto is the Jeroslow&Wang method\n");
    //printf("  -decay         = <num> [ 0 - 1 ]\n");
    printf("  --rnd-freq       = <num> [ 0 - 1 ]\n");
    printf("  --verbosity      = {0,1,2}\n");
    #ifdef STATS_NEEDED
    printf("  --proof-log      = Logs the proof into files 'proofN.dot', where N is the\n");
    printf("                     restart number. The log can then be visualized using\n");
    printf("                     the 'dot' program from the graphviz package\n");
    printf("  --grouping       = Lets you group clauses, and customize the groups' names.\n");
    printf("                     This helps when printing statistics\n");
    printf("  --stats          = Computes and prints statistics during the search\n");
    #endif
    printf("  --randomize      = <seed> [0 - 2^32-1] Sets random seed, used for picking\n");
    printf("                     decision variables (default = 0)\n");
    printf("  --restrict       = <num> [1 - varnum] when picking random variables to branch\n");
    printf("                     on, pick one that in the 'num' most active vars useful\n");
    printf("                     for cryptographic problems, where the question is the key,\n");
    printf("                     which is usually small (e.g. 80 bits)\n");
    printf("  --gaussuntil     = <num> Depth until which Gaussian elimination is active.\n");
    printf("                     Giving 0 switches off Gaussian elimination\n");
    printf("  --restarts       = <num> [1 - 2^32-1] No more than the given number of\n");
    printf("                     restarts will be performed during search\n");
    printf("  --nonormxorfind  = Don't find and collect >2-long xor-clauses from\n");
    printf("                     regular clauses\n");
    printf("  --nobinxorfind   = Don't find and collect 2-long xor-clauses from\n");
    printf("                     regular clauses\n");
    printf("  --noregbxorfind  = Don't regularly find and collect 2-long xor-clauses\n");
    printf("                     from regular clauses\n");
    printf("  --noconglomerate = Don't conglomerate 2 xor clauses when one var is dependent\n");
    printf("  --nosimplify     = Don't do regular simplification rounds\n");
    printf("  --greedyunbound  = Greedily unbound variables that are not needed for SAT\n");
    printf("  --debuglib       = Solve at specific 'c Solver::solve()' points in the CNF\n");
    printf("                     file. Used to debug file generated by Solver's\n");
    printf("                     needLibraryCNFFile() function\n");
    printf("  --debugnewvar    = Add new vars at specific 'c Solver::newVar()' points in \n");
    printf("                     the CNF file. Used to debug file generated by Solver's\n");
    printf("                     needLibraryCNFFile() function.\n");
    printf("  --novarreplace   = Don't perform variable replacement. Needed for programmable\n");
    printf("                     solver feature\n");
    printf("  --restart        = {auto, static, dynamic}   Which kind of restart strategy to\n");
    printf("                     follow. Default is auto\n");
    printf("  --dumplearnts    = <filename> If interrupted or reached restart limit, dump\n");
    printf("                     the learnt clauses to the specified file. Maximum size of\n");
    printf("                     dumped clauses can be specified with next option.\n");
    printf("  --maxdumplearnts = [0 - 2^32-1] When dumping the learnts to file, what\n");
    printf("                     should be maximum length of the clause dumped. Useful\n");
    printf("                     to make the resulting file smaller. Default is 2^32-1\n");
    printf("                     note: 2-long XOR-s are always dumped.\n");
    printf("  --dumporig       = <filename> If interrupted or reached restart limit, dump\n");
    printf("                     the original problem instance, simplified to the\n");
    printf("                     current point.\n");
    printf("  --alsoread       = <filename> Also read this file in\n");
    printf("                     Can be used to re-read dumped learnts, for example\n");
    printf("  --maxsolutions   = Search for given amount of solutions\n");
    printf("  --nofailedvar    = Don't search for failed vars, and don't search for vars\n");
    printf("                     doubly propagated to the same value\n");
    printf("  --noheuleprocess = Don't try to minimise XORs by XOR-ing them together.\n");
    printf("                     Algo. as per global/local substitution in Heule's thesis\n");
    printf("  --nosatelite     = Don't do clause subsumption, clause strengthening and\n");
    printf("                     variable elimination (implies -novarelim and -nosubsume1).\n");
    printf("  --noxorsubs      = Don't try to subsume xor-clauses.\n");
    printf("  --nosolprint     = Don't print the satisfying assignment if the solution\n");
    printf("                     is SAT\n");
    printf("  --novarelim      = Don't perform variable elimination as per Een and Biere\n");
    printf("  --nosubsume1     = Don't perform clause contraction through resolution\n");
    printf("  --noparthandler  = Don't find and solve subroblems with subsolvers\n");
#ifdef USE_GAUSS
    printf("  --nomatrixfind   = Don't find distinct matrixes. Put all xors into one\n");
    printf("                     big matrix\n");
    printf("  --noordercol     = Don't order variables in the columns of Gaussian\n");
    printf("                     elimination. Effectively disables iterative reduction\n");
    printf("                     of the matrix\n");
    printf("  --noiterreduce   = Don't reduce iteratively the matrix that is updated\n");
    printf("  --maxmatrixrows  = [0 - 2^32-1] Set maximum no. of rows for gaussian matrix.\n");
    printf("                     Too large matrixes should bee discarded for\n");
    printf("                     reasons of efficiency. Default: %d\n", gaussconfig.maxMatrixRows);
    printf("  --minmatrixrows  = [0 - 2^32-1] Set minimum no. of rows for gaussian matrix.\n");
    printf("                     Normally, too small matrixes are discarded for\n");
    printf("                     reasons of efficiency. Default: %d\n", gaussconfig.minMatrixRows);
    printf("  --savematrix     = [0 - 2^32-1] Save matrix every Nth decision level.\n");
    printf("                     Default: %d\n", gaussconfig.only_nth_gauss_save);
    printf("  --maxnummatrixes = [0 - 2^32-1] Maximum number of matrixes to treat.\n");
    printf("                     Default: %d\n", gaussconfig.maxNumMatrixes);
#endif //USE_GAUSS
    //printf("  --addoldlearnts  = Readd old learnts for failed variable searching.\n");
    //printf("                     These learnts are usually deleted, but may help\n");
    printf("  --nohyperbinres  = Don't add binary clauses when doing failed lit probing.\n");
    printf("  --noremovebins   = Don't remove useless binary clauses at the beginnning\n");
    printf("  --noregremovebins= Don't remove useless binary clauses regularly\n");
    printf("  --nosubswithbins = Don't subsume with non-existent bins at the beginnning\n");
    printf("  --norsubswithbins= Don't subsume with non-existent bins regularly \n");
    printf("  --noasymm        = Don't do asymmetric branching at the beginnning\n");
    printf("  --norasymm       = Don't do asymmetric branching regularly\n");
    printf("  --nosortwatched  = Don't sort watches according to size: bin, tri, etc.\n");
    printf("  --nolfminim      = Don't do on-the-fly self-subsuming resolution\n");
    printf("                     (called 'strong minimisation' in PrecoSat)\n");
    printf("  --lfminimrec     = Always perform recursive/transitive OTF self-\n");
    printf("                     subsuming resolution (enhancement of \n");
    printf("                     'strong minimisation' in PrecoSat)\n");
    printf("  --maxglue        = [0 - 2^32-1] default: %d. Glue value above which we\n", conf.maxGlue);
    printf("                     throw the clause away on backtrack. Only active\n");
    printf("                     when dynamic restarts have been selected\n");
    printf("\n");
}


const char* Main::hasPrefix(const char* str, const char* prefix)
{
    int len = strlen(prefix);
    if (strncmp(str, prefix, len) == 0)
        return str + len;
    else
        return NULL;
}

void Main::printResultFunc(const Solver& S, const lbool ret, FILE* res)
{
    if (res != NULL) {
        if (ret == l_True) {
            printf("c SAT\n");
            fprintf(res, "SAT\n");
            if (printResult) {
                for (Var var = 0; var != S.nVars(); var++)
                    if (S.model[var] != l_Undef)
                        fprintf(res, "%s%d ", (S.model[var] == l_True)? "" : "-", var+1);
                    fprintf(res, "0\n");
            }
        } else if (ret == l_False) {
            printf("c UNSAT\n");
            fprintf(res, "UNSAT\n");
        } else {
            printf("c INCONCLUSIVE\n");
            fprintf(res, "INCONCLUSIVE\n");
        }
        fclose(res);
    } else {
        if (ret == l_True)
            printf("s SATISFIABLE\n");
        else if (ret == l_False)
            printf("s UNSATISFIABLE\n");

        if(ret == l_True && printResult) {
            printf("v ");
            for (Var var = 0; var != S.nVars(); var++)
                if (S.model[var] != l_Undef)
                    printf("%s%d ", (S.model[var] == l_True)? "" : "-", var+1);
                printf("0\n");
        }
    }
}

void Main::parseCommandLine()
{
    const char* value;
    char tmpFilename[201];
    tmpFilename[0] = '\0';
    uint32_t unparsedOptions = 0;
    bool needTwoFileNames = false;
    conf.verbosity = 2;

    for (int i = 0; i < argc; i++) {
        if ((value = hasPrefix(argv[i], "--polarity-mode="))) {
            if (strcmp(value, "true") == 0)
                conf.polarity_mode = polarity_true;
            else if (strcmp(value, "false") == 0)
                conf.polarity_mode = polarity_false;
            else if (strcmp(value, "rnd") == 0)
                conf.polarity_mode = polarity_rnd;
            else if (strcmp(value, "auto") == 0)
                conf.polarity_mode = polarity_auto;
            else {
                printf("ERROR! unknown polarity-mode %s\n", value);
                exit(0);
            }

        } else if ((value = hasPrefix(argv[i], "--rnd-freq="))) {
            double rnd;
            if (sscanf(value, "%lf", &rnd) <= 0 || rnd < 0 || rnd > 1) {
                printf("ERROR! illegal rnRSE ERROR!d-freq constant %s\n", value);
                exit(0);
            }
            conf.random_var_freq = rnd;

        /*} else if ((value = hasPrefix(argv[i], "--decay="))) {
            double decay;
            if (sscanf(value, "%lf", &decay) <= 0 || decay <= 0 || decay > 1) {
                printf("ERROR! illegal decay constant %s\n", value);
                exit(0);
            }
            conf.var_decay = 1 / decay;*/

        } else if ((value = hasPrefix(argv[i], "--verbosity="))) {
            int verbosity = (int)strtol(value, NULL, 10);
            if (verbosity == EINVAL || verbosity == ERANGE) {
                printf("ERROR! illegal verbosity level %s\n", value);
                exit(0);
            }
            conf.verbosity = verbosity;
        #ifdef STATS_NEEDED
        } else if ((value = hasPrefix(argv[i], "--grouping"))) {
            grouping = true;
        } else if ((value = hasPrefix(argv[i], "--proof-log"))) {
            conf.needProofGraph();

        } else if ((value = hasPrefix(argv[i], "--stats"))) {
            conf.needStats();
        #endif

        } else if ((value = hasPrefix(argv[i], "--randomize="))) {
            uint32_t seed;
            if (sscanf(value, "%d", &seed) < 0) {
                printf("ERROR! illegal seed %s\n", value);
                exit(0);
            }
            conf.origSeed = seed;
        } else if ((value = hasPrefix(argv[i], "--restrict="))) {
            uint32_t branchTo;
            if (sscanf(value, "%d", &branchTo) < 0 || branchTo < 1) {
                printf("ERROR! illegal restricted pick branch number %d\n", branchTo);
                exit(0);
            }
            conf.restrictPickBranch = branchTo;
        } else if ((value = hasPrefix(argv[i], "--gaussuntil="))) {
            uint32_t until;
            if (sscanf(value, "%d", &until) < 0) {
                printf("ERROR! until %s\n", value);
                exit(0);
            }
            gaussconfig.decision_until = until;
        } else if ((value = hasPrefix(argv[i], "--restarts="))) {
            uint32_t maxrest;
            if (sscanf(value, "%d", &maxrest) < 0 || maxrest == 0) {
                printf("ERROR! illegal maximum restart number %d\n", maxrest);
                exit(0);
            }
            conf.maxRestarts = maxrest;
        } else if ((value = hasPrefix(argv[i], "--dumplearnts="))) {
            if (sscanf(value, "%200s", tmpFilename) < 0 || strlen(tmpFilename) == 0) {
                printf("ERROR! wrong filename '%s'\n", tmpFilename);
                exit(0);
            }
            conf.learntsFilename.assign(tmpFilename);
            conf.needToDumpLearnts = true;
        } else if ((value = hasPrefix(argv[i], "--dumporig="))) {
            if (sscanf(value, "%200s", tmpFilename) < 0 || strlen(tmpFilename) == 0) {
                printf("ERROR! wrong filename '%s'\n", tmpFilename);
                exit(0);
            }
            conf.origFilename.assign(tmpFilename);
            conf.needToDumpOrig = true;
        } else if ((value = hasPrefix(argv[i], "--alsoread="))) {
            if (sscanf(value, "%400s", tmpFilename) < 0 || strlen(tmpFilename) == 0) {
                printf("ERROR! wrong filename '%s'\n", tmpFilename);
                exit(0);
            }
            filesToRead.push_back(tmpFilename);
        } else if ((value = hasPrefix(argv[i], "--maxdumplearnts="))) {
            if (!conf.needToDumpLearnts) {
                printf("ERROR! -dumplearnts=<filename> must be first activated before issuing -maxdumplearnts=<size>\n");
                exit(0);
            }
            int tmp;
            if (sscanf(value, "%d", &tmp) < 0 || tmp < 0) {
                std::cout << "ERROR! wrong maximum dumped learnt clause size is illegal: " << tmp << std::endl;
                exit(0);
            }
            conf.maxDumpLearntsSize = (uint32_t)tmp;
        } else if ((value = hasPrefix(argv[i], "--maxsolutions="))) {
            int tmp;
            if (sscanf(value, "%d", &tmp) < 0 || tmp < 0) {
                std::cout << "ERROR! wrong maximum number of solutions is illegal: " << tmp << std::endl;
                exit(0);
            }
            max_nr_of_solutions = (uint32_t)tmp;
        } else if ((value = hasPrefix(argv[i], "--greedyunbound"))) {
            conf.greedyUnbound = true;
        } else if ((value = hasPrefix(argv[i], "--nonormxorfind"))) {
            conf.doFindXors = false;
        } else if ((value = hasPrefix(argv[i], "--nobinxorfind"))) {
            conf.doFindEqLits = false;
        } else if ((value = hasPrefix(argv[i], "--noregbxorfind"))) {
            conf.doRegFindEqLits = false;
        } else if ((value = hasPrefix(argv[i], "--noconglomerate"))) {
            conf.doConglXors = false;
        } else if ((value = hasPrefix(argv[i], "--nosimplify"))) {
            conf.doSchedSimp = false;
        } else if ((value = hasPrefix(argv[i], "--debuglib"))) {
            debugLib = true;
        } else if ((value = hasPrefix(argv[i], "--debugnewvar"))) {
            debugNewVar = true;
        } else if ((value = hasPrefix(argv[i], "--novarreplace"))) {
            conf.doReplace = false;
        } else if ((value = hasPrefix(argv[i], "--nofailedvar"))) {
            conf.doFailedLit = false;
        } else if ((value = hasPrefix(argv[i], "--nodisablegauss"))) {
            gaussconfig.dontDisable = true;
        } else if ((value = hasPrefix(argv[i], "--maxnummatrixes="))) {
            uint32_t maxNumMatrixes;
            if (sscanf(value, "%d", &maxNumMatrixes) < 0) {
                printf("ERROR! maxnummatrixes: %s\n", value);
                exit(0);
            }
            gaussconfig.maxNumMatrixes = maxNumMatrixes;
        } else if ((value = hasPrefix(argv[i], "--noheuleprocess"))) {
            conf.doHeuleProcess = false;
        } else if ((value = hasPrefix(argv[i], "--nosatelite"))) {
            conf.doSatELite = false;
        } else if ((value = hasPrefix(argv[i], "--noparthandler"))) {
            conf.doPartHandler = false;
        } else if ((value = hasPrefix(argv[i], "--noxorsubs"))) {
            conf.doXorSubsumption = false;
        } else if ((value = hasPrefix(argv[i], "--nohyperbinres"))) {
            conf.doHyperBinRes = false;
        } else if ((value = hasPrefix(argv[i], "--noblockedclause"))) {
            conf.doBlockedClause = false;
        } else if ((value = hasPrefix(argv[i], "--novarelim"))) {
            conf.doVarElim = false;
        } else if ((value = hasPrefix(argv[i], "--nosubsume1"))) {
            conf.doSubsume1 = false;
        } else if ((value = hasPrefix(argv[i], "--nomatrixfind"))) {
            gaussconfig.noMatrixFind = true;
        } else if ((value = hasPrefix(argv[i], "--noiterreduce"))) {
            gaussconfig.iterativeReduce = false;
        } else if ((value = hasPrefix(argv[i], "--noiterreduce"))) {
            gaussconfig.iterativeReduce = false;
        } else if ((value = hasPrefix(argv[i], "--noordercol"))) {
            gaussconfig.orderCols = false;
        } else if ((value = hasPrefix(argv[i], "--maxmatrixrows"))) {
            uint32_t rows;
            if (sscanf(value, "%d", &rows) < 0) {
                printf("ERROR! maxmatrixrows: %s\n", value);
                exit(0);
            }
            gaussconfig.maxMatrixRows = rows;
        } else if ((value = hasPrefix(argv[i], "--minmatrixrows"))) {
            uint32_t rows;
            if (sscanf(value, "%d", &rows) < 0) {
                printf("ERROR! minmatrixrows: %s\n", value);
                exit(0);
            }
            gaussconfig.minMatrixRows = rows;
        } else if ((value = hasPrefix(argv[i], "--savematrix"))) {
            uint32_t every;
            if (sscanf(value, "%d", &every) < 0) {
                printf("ERROR! savematrix: %s\n", value);
                exit(0);
            }
            std::cout << "c Matrix saved every " <<  every << " decision levels" << std::endl;
            gaussconfig.only_nth_gauss_save = every;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv);
            exit(0);
        } else if ((value = hasPrefix(argv[i], "--restart="))) {
            if (strcmp(value, "auto") == 0)
                conf.fixRestartType = auto_restart;
            else if (strcmp(value, "static") == 0)
                conf.fixRestartType = static_restart;
            else if (strcmp(value, "dynamic") == 0)
                conf.fixRestartType = dynamic_restart;
            else {
                printf("ERROR! unknown restart type %s\n", value);
                exit(0);
            }
        } else if ((value = hasPrefix(argv[i], "--nosolprint"))) {
            printResult = false;
        //} else if ((value = hasPrefix(argv[i], "--addoldlearnts"))) {
        //    conf.readdOldLearnts = true;
        } else if ((value = hasPrefix(argv[i], "--nohyperbinres"))) {
            conf.doHyperBinRes= false;
        } else if ((value = hasPrefix(argv[i], "--noremovebins"))) {
            conf.doRemUselessBins = false;
        } else if ((value = hasPrefix(argv[i], "--nosubswithbins"))) {
            conf.doSubsWNonExistBins = false;
        } else if ((value = hasPrefix(argv[i], "--noasymm"))) {
            conf.doAsymmBranch = false;
        } else if ((value = hasPrefix(argv[i], "--nosortwatched"))) {
            conf.doSortWatched = false;
        } else if ((value = hasPrefix(argv[i], "--nolfminim"))) {
            conf.doMinimLearntMore = false;
        } else if ((value = hasPrefix(argv[i], "--lfminimrec"))) {
            conf.doMinimLMoreRecur = true;
        } else if ((value = hasPrefix(argv[i], "--maxglue="))) {
            int glue = 16;
            if (sscanf(value, "%d", &glue) < 0 || glue < 0) {
                printf("ERROR! maxGlue: %s\n", value);
                exit(0);
            }
            if (glue >= (1<< MAX_GLUE_BITS)-1) {
                std::cout << "Due to memory-packing limitations, max glue cannot be more than "
                << ((1<< MAX_GLUE_BITS)-2) << std::endl;
                exit(-1);
            }
            conf.maxGlue = (uint32_t)glue;
        } else if (strncmp(argv[i], "-", 1) == 0 || strncmp(argv[i], "--", 2) == 0) {
            printf("ERROR! unknown flag %s\n", argv[i]);
            exit(0);
        } else {
            //std::std::cout << "argc:" << argc << " i:" << i << ", value:" << argv[i] << std::endl;
            unparsedOptions++;
            if (unparsedOptions == 2) {
                if (!(argc <= i+2)) {
                    std::cout << "You must give the input file as either:" << std::endl;
                    std::cout << " -- last option if you want the output to the console" << std::endl;
                    std::cout << " -- or one before the last option" << std::endl;
                    std::cout << "It appears that you did neither. Maybe you forgot the '--' from an option?" << std::endl;
                    exit(-1);
                }
                fileNamePresent = true;
                if (argc == i+2) needTwoFileNames = true;
            }
            if (unparsedOptions == 3) {
                if (!(argc <= i+1)) {
                    std::cout << "You must give the output file as the last option. Exiting" << std::endl;
                    exit(-1);
                }
                twoFileNamesPresent = true;
            }
            if (unparsedOptions == 4) {
                std::cout << "You gave more than two filenames as parameters." << std::endl;
                std::cout << "The first one is interpreted as the input, the second is the output." << std::endl;
                std::cout << "However, the third one I cannot do anything with. EXITING" << std::endl;
                exit(-1);
            }
        }
    }
    if (conf.verbosity >= 1) {
        if (twoFileNamesPresent) {
            std::cout << "c Outputting solution to file: " << argv[argc-1] << std::endl;
        } else {
            std::cout << "c Ouptutting solution to console" << std::endl;
        }
    }

    if (unparsedOptions == 2 && needTwoFileNames == true) {
        std::cout << "Command line wrong. You probably frogot to add "<< std::endl
        << "the '--'  in front of one of the options, or you started" << std::endl
        << "your output file with a hyphen ('-'). Exiting." << std::endl;
        exit(-1);
    }
    if (!debugLib) conf.libraryUsage = false;
}

FILE* Main::openOutputFile()
{
    FILE* res = NULL;
    if (twoFileNamesPresent) {
        char* filename = argv[argc-1];
        res = fopen(filename, "wb");
        if (res == NULL) {
            int backup_errno = errno;
            printf("Cannot open %s for writing. Problem: %s", filename, strerror(backup_errno));
            exit(1);
        }
    }

    return res;
}

void Main::setDoublePrecision(const uint32_t verbosity)
{
    #if defined(__linux__)
    fpu_control_t oldcw, newcw;
    _FPU_GETCW(oldcw);
    newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE;
    _FPU_SETCW(newcw);
    if (verbosity >= 1) printf("c WARNING: for repeatability, setting FPU to use double precision\n");
#endif
}

void Main::printVersionInfo(const uint32_t verbosity)
{
    if (verbosity >= 1) printf("c This is CryptoMiniSat %s\n", VERSION);
}

int Main::singleThreadSolve()
{
    Solver solver(conf);
    solverToInterrupt = &solver;

    printVersionInfo(conf.verbosity);
    setDoublePrecision(conf.verbosity);

    parseInAllFiles(solver);
    FILE* res = openOutputFile();

    unsigned long current_nr_of_solutions = 0;
    lbool ret = l_True;
    while(current_nr_of_solutions < max_nr_of_solutions && ret == l_True) {
        ret = solver.solve();
        current_nr_of_solutions++;

        if (ret == l_True && current_nr_of_solutions < max_nr_of_solutions) {
            if (conf.verbosity >= 1) std::cout << "c Prepare for next run..." << std::endl;
            printResultFunc(solver, ret, res);

            vec<Lit> lits;
            for (Var var = 0; var != solver.nVars(); var++) {
                if (solver.model[var] != l_Undef) {
                    lits.push( Lit(var, (solver.model[var] == l_True)? true : false) );
                }
            }
            solver.addClause(lits);
        }
    }

    if (conf.needToDumpLearnts) {
        solver.dumpSortedLearnts(conf.learntsFilename, conf.maxDumpLearntsSize);
        std::cout << "c Sorted learnt clauses dumped to file '" << conf.learntsFilename << "'" << std::endl;
    }
    if (conf.needToDumpOrig) {
        solver.dumpOrigClauses(conf.origFilename);
        std::cout << "c Simplified original clauses dumped to file '" << conf.origFilename << "'" << std::endl;
    }
    if (ret == l_Undef && conf.verbosity >= 1) {
        std::cout << "c Not finished running -- maximum restart reached" << std::endl;
    }
    printResultFunc(solver, ret, res);
    if (conf.verbosity >= 1) printStats(solver);

    return correctReturnValue(ret);
}

int Main::correctReturnValue(const lbool ret) const
{
    int retval = -1;
    if      (ret == l_True)  retval = 10;
    else if (ret == l_False) retval = 20;
    else if (ret == l_Undef) retval = 15;
    else {
        std::cerr << "Something is very wrong, output is neither l_Undef, nor l_False, nor l_True" << std::endl;
        exit(-1);
    }

    #ifdef NDEBUG
    // (faster than "return", which will invoke the destructor for 'Solver')
    exit(retval);
    #endif
    return retval;
}

const int Main::oneThreadSolve()
{
    SolverConf myConf = conf;
    uint32_t num = omp_get_thread_num();
    myConf.origSeed = num;
    if (num > 0) {
        if (num % 2) myConf.fixRestartType = dynamic_restart;
        else myConf.fixRestartType = static_restart;
        myConf.simpStartMult *= 2*(num+1);
        myConf.simpStartMMult *= 2*(num+1);
    }
    if (num != 0) myConf.verbosity = 0;

    Solver solver(myConf);
    if (num == 0) solverToInterrupt = &solver;

    printVersionInfo(myConf.verbosity);
    setDoublePrecision(myConf.verbosity);

    parseInAllFiles(solver);
    lbool ret = solver.solve();

    FILE* res = openOutputFile();
    printResultFunc(solver, ret, res);
    printStats(solver);

    int retval = correctReturnValue(ret);
    exit(retval);
    return retval;
}

int Main::multiThreadSolve(const uint32_t numThreads)
{
    #pragma omp parallel
    {
        #pragma omp single
        {
            if (conf.verbosity >= 1)
                std::cout << "c Using " << omp_get_num_threads()
                << " threads" << std::endl;
        }
        oneThreadSolve();
    }

    return 0;
}

int main(int argc, char** argv)
{
    Main main(argc, argv);
    main.parseCommandLine();
    signal(SIGINT, SIGINT_handler);
    //signal(SIGHUP,SIGINT_handler);

    return main.multiThreadSolve(4);
    //return main.singleThreadSolve();

}
