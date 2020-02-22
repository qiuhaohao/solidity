//
// Created by Qiu Haoze on 13/10/19.
//
#include <solv/CommandLineInterface.h>

#include "license.h"

#include <libsolidity/interface/Version.h>
#include <libsolidity/ast/ASTPrinter.h>

#include <libevmasm/GasMeter.h>

#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatter.h>
#include <liblangutil/SourceReferenceFormatterHuman.h>

#include <libdevcore/CommonIO.h>

#include <boost/filesystem.hpp>

#include <solv/task/ITask.h>
#include <solv/task/TaskFinder.h>
#include <solv/task/IReportItem.h>

using namespace std;
using namespace langutil;
namespace po = boost::program_options;

namespace dev {
namespace solidity {
namespace verifier {

bool g_hasOutput = false;

std::ostream& sout()
{
    g_hasOutput = true;
    return cout;
}

std::ostream& serr(bool _used = true)
{
    if (_used)
        g_hasOutput = true;
    return cerr;
}

#define cout
#define cerr

static string const g_stdinFileNameStr = "<stdin>";
static string const g_strAst = "ast";
static string const g_strInputFile = "input-file";
static string const g_strLicense = "license";
static string const g_strEVMVersion = "evm-version";
static string const g_strHelp = "help";
static string const g_strVersion = "version";
static string const g_strIgnoreMissingFiles = "ignore-missing";
static string const g_strColor = "color";
static string const g_strNoColor = "no-color";
static string const g_strNewReporter = "new-reporter";

static string const g_argAst = g_strAst;
static string const g_argInputFile = g_strInputFile;
static string const g_argHelp = g_strHelp;
static string const g_argVersion = g_strVersion;
static string const g_stdinFileName = g_stdinFileNameStr;
static string const g_argIgnoreMissingFiles = g_strIgnoreMissingFiles;
static string const g_argColor = g_strColor;
static string const g_argNoColor = g_strNoColor;
static string const g_argNewReporter = g_strNewReporter;

static void version()
{
    sout() <<
           "solv, the solidity verifier commandline  interface" <<
           endl <<
           "Version: " <<
           dev::solidity::VersionString <<
           endl;
    exit(0);
}

static void license()
{
    sout() << otherLicenses << endl;
    // This is a static variable generated by cmake from LICENSE.txt
    sout() << licenseText << endl;
    exit(0);
}

bool CommandLineInterface::readInputFilesAndConfigureRemappings()
{
    bool ignoreMissing = m_args.count(g_argIgnoreMissingFiles);
    bool addStdin = false;
    if (m_args.count(g_argInputFile))
        for (string path: m_args[g_argInputFile].as<vector<string>>())
        {
            auto eq = find(path.begin(), path.end(), '=');
            if (eq != path.end())
            {
                if (auto r = CompilerStack::parseRemapping(path))
                {
                    m_remappings.emplace_back(std::move(*r));
                    path = string(eq + 1, path.end());
                }
                else
                {
                    serr() << "Invalid remapping: \"" << path << "\"." << endl;
                    return false;
                }
            }
            else if (path == "-")
                addStdin = true;
            else
            {
                auto infile = boost::filesystem::path(path);
                if (!boost::filesystem::exists(infile))
                {
                    if (!ignoreMissing)
                    {
                        serr() << infile << " is not found." << endl;
                        return false;
                    }
                    else
                        serr() << infile << " is not found. Skipping." << endl;

                    continue;
                }

                if (!boost::filesystem::is_regular_file(infile))
                {
                    if (!ignoreMissing)
                    {
                        serr() << infile << " is not a valid file." << endl;
                        return false;
                    }
                    else
                        serr() << infile << " is not a valid file. Skipping." << endl;

                    continue;
                }

                m_sourceCodes[infile.generic_string()] = dev::readFileAsString(infile.string());
                path = boost::filesystem::canonical(infile).string();
            }
            m_allowedDirectories.push_back(boost::filesystem::path(path).remove_filename());
        }
    if (addStdin)
        m_sourceCodes[g_stdinFileName] = dev::readStandardInput();
    if (m_sourceCodes.size() == 0)
    {
        serr() << "No input files given. If you wish to use the standard input please specify \"-\" explicitly." << endl;
        return false;
    }

    return true;
}

bool CommandLineInterface::parseArguments(int _argc, char** _argv)
{
    g_hasOutput = false;

    // Declare the supported options.
    po::options_description desc(R"(solv, the Solidity commandline verifier.

This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you
are welcome to redistribute it under certain conditions. See 'solc --license'
for details.

Usage: solv [options] [input_file...]
Verifies the given Solidity input files (or the standard input if none given or
"-" is used as a file name) and outputs the verification report.
Imports are automatically read from the filesystem, but it is also possible to
remap paths using the context:prefix=path syntax.
Example:
solv contract.sol

Allowed options)",
         po::options_description::m_default_line_length,
         po::options_description::m_default_line_length - 23
    );

    // Allowed options:
    desc.add_options()
        (g_argHelp.c_str(), "Show help message and exit.")
        (g_argVersion.c_str(), "Show version and exit.")
        (g_strLicense.c_str(), "Show licensing information and exit.")
        (
            g_strEVMVersion.c_str(),
            po::value<string>()->value_name("version"),
            "Select desired EVM version. Either homestead, tangerineWhistle, spuriousDragon, byzantium (default) or constantinople."
        )
        (g_argNewReporter.c_str(), "Enables new diagnostics reporter.")
        (g_argColor.c_str(), "Force colored output.")
        (g_argNoColor.c_str(), "Explicitly disable colored output, disabling terminal auto-detection.")
        (g_argIgnoreMissingFiles.c_str(), "Ignore missing files.");
    // Output Components:
    po::options_description outputComponents("Output Components");
    outputComponents.add_options()
            (g_argAst.c_str(), "AST of all source files.");
    desc.add(outputComponents);
    // Input file:
    po::options_description allOptions = desc;
    allOptions.add_options()(g_argInputFile.c_str(), po::value<vector<string>>(), "input file");

    // Only one file allowed
    po::positional_options_description filesPositions;
    filesPositions.add(g_argInputFile.c_str(), 1);

    // parse the compiler arguments
    try
    {
        po::command_line_parser cmdLineParser(_argc, _argv);
        cmdLineParser.style(po::command_line_style::default_style & (~po::command_line_style::allow_guessing));
        cmdLineParser.options(allOptions).positional(filesPositions);
        po::store(cmdLineParser.run(), m_args);
    }
    catch (po::error const& _exception)
    {
        serr() << _exception.what() << endl;
        return false;
    }

    if (m_args.count(g_argColor) && m_args.count(g_argNoColor))
    {
        serr() << "Option " << g_argColor << " and " << g_argNoColor << " are mutualy exclusive." << endl;
        return false;
    }

    m_coloredOutput = !m_args.count(g_argNoColor) && (isatty(STDERR_FILENO) || m_args.count(g_argColor));

    if (m_args.count(g_argHelp) || (isatty(fileno(stdin)) && _argc == 1))
    {
        sout() << desc;
        return false;
    }

    if (m_args.count(g_argVersion))
    {
        version();
        return false;
    }

    if (m_args.count(g_strLicense))
    {
        license();
        return false;
    }

    return true;
}

bool CommandLineInterface::processInput()
{
    ReadCallback::Callback fileReader = [this](string const& _path)
    {
        try
        {
            auto path = boost::filesystem::path(_path);
            auto canonicalPath = boost::filesystem::weakly_canonical(path);
            bool isAllowed = false;
            for (auto const& allowedDir: m_allowedDirectories)
            {
                // If dir is a prefix of boostPath, we are fine.
                if (
                        std::distance(allowedDir.begin(), allowedDir.end()) <= std::distance(canonicalPath.begin(), canonicalPath.end()) &&
                        std::equal(allowedDir.begin(), allowedDir.end(), canonicalPath.begin())
                        )
                {
                    isAllowed = true;
                    break;
                }
            }
            if (!isAllowed)
                return ReadCallback::Result{false, "File outside of allowed directories."};

            if (!boost::filesystem::exists(canonicalPath))
                return ReadCallback::Result{false, "File not found."};

            if (!boost::filesystem::is_regular_file(canonicalPath))
                return ReadCallback::Result{false, "Not a valid file."};

            auto contents = dev::readFileAsString(canonicalPath.string());
            sout() << contents;
            m_sourceCodes[path.generic_string()] = contents;
            return ReadCallback::Result{true, contents};
        }
        catch (Exception const& _exception)
        {
            return ReadCallback::Result{false, "Exception in read callback: " + boost::diagnostic_information(_exception)};
        }
        catch (...)
        {
            return ReadCallback::Result{false, "Unknown exception in read callback."};
        }
    };

    if (!readInputFilesAndConfigureRemappings())
        return false;

    if (m_args.count(g_strEVMVersion))
    {
        string versionOptionStr = m_args[g_strEVMVersion].as<string>();
        boost::optional<langutil::EVMVersion> versionOption = langutil::EVMVersion::fromString(versionOptionStr);
        if (!versionOption)
        {
            serr() << "Invalid option for --evm-version: " << versionOptionStr << endl;
            return false;
        }
        m_evmVersion = *versionOption;
    }

    m_compiler.reset(new CompilerStack(fileReader));

    unique_ptr<SourceReferenceFormatter> formatter;
    if (m_args.count(g_argNewReporter))
        formatter = make_unique<SourceReferenceFormatterHuman>(serr(false), m_coloredOutput);
    else
        formatter = make_unique<SourceReferenceFormatter>(serr(false));

    try
    {
        if (m_args.count(g_argInputFile))
            m_compiler->setRemappings(m_remappings);
        m_compiler->setSources(m_sourceCodes);
        bool successful = m_compiler->parseAndAnalyze();
        if(!successful){
            serr() << "parsing error!!"<<std::endl;

            for (auto const& error: m_compiler->errors()) {

                formatter->printExceptionInformation(
                        *error,
                        (error->type() == langutil::Error::Type::Warning) ? "Warning" : "Error"
                );
            }

            return -1;
        }
    }
    catch (CompilerError const& _exception)
    {
        g_hasOutput = true;
        formatter->printExceptionInformation(_exception, "Compiler error");
        return false;
    }
    catch (InternalCompilerError const& _exception)
    {
        serr() <<
               "Internal compiler error during compilation:" <<
               endl <<
               boost::diagnostic_information(_exception);
        return false;
    }
    catch (UnimplementedFeatureError const& _exception)
    {
        serr() <<
               "Unimplemented feature:" <<
               endl <<
               boost::diagnostic_information(_exception);
        return false;
    }
    catch (Error const& _error)
    {
        if (_error.type() == Error::Type::DocstringParsingError)
            serr() << "Documentation parsing error: " << *boost::get_error_info<errinfo_comment>(_error) << endl;
        else
        {
            g_hasOutput = true;
            formatter->printExceptionInformation(_error, _error.typeName());
        }

        return false;
    }
    catch (Exception const& _exception)
    {
        serr() << "Exception during parsing: " << boost::diagnostic_information(_exception) << endl;
        return false;
    }
    catch (std::exception const& _e)
    {
        serr() << "Unknown exception during parsing" << (
                _e.what() ? ": " + string(_e.what()) : "."
        ) << endl;
        return false;
    }
    catch (...)
    {
        serr() << "Unknown exception during parsing." << endl;
        return false;
    }

    return true;
}

void CommandLineInterface::handleAst()
{
    string title = "Syntax trees:";

    sout() << title << endl << endl;
    for (auto const& sourceCode: m_sourceCodes)
    {
        sout() << endl << "======= " << sourceCode.first << " =======" << endl;
        sout() << sourceCode.second;
            ASTPrinter printer(m_compiler->ast(sourceCode.first), sourceCode.second);
            printer.print(sout());
    }
}

void CommandLineInterface::outputResults()
{
    // do we need AST output?
    if (m_args.count(g_argAst))
        handleAst();
    else {
        for (auto const& sourceCode: m_sourceCodes)
        {
            sout() << endl << "======= " << sourceCode.first << " =======" << endl;
            const SourceUnit& sourceUnit = m_compiler->ast(sourceCode.first);
            vector<ITask*> tasks = TaskFinder::findTasks(sourceCode.second, sourceUnit);
            for (auto task : tasks) {
                vector<IReportItem*> reportItems = task->execute();
                for (auto reportItem : reportItems) {
                    reportItem->report();
                }
            }
        }
    }
}

bool CommandLineInterface::actOnInput()
{
    outputResults();
    return true;
}

}
}
}
