// =============================================================================
//  switchblade-analyze
//
//  CLI frontend for the Analysis Engine. Reads an audio file, runs transient
//  detection (or texture analysis when implemented), and emits a JSON report.
//
//  Usage:
//      switchblade-analyze <in.wav> [out.json]
//                          [--mode=auto|percussive|melodic|texture]
//                          [--sensitivity=1.0]   (0.3 .. 3.0)
//                          [--min-spacing=40]    (ms)
//                          [--snap=5]            (ms, zero-cross search)
//
//  Exit codes:
//      0   success
//      1   CLI / usage error
//      2   analysis failure (I/O, format, empty result)
// =============================================================================

#include "Analysis/AnalysisEngine.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void printUsage (std::ostream& os)
    {
        os <<
            "switchblade-analyze — transient / texture analysis CLI\n"
            "\n"
            "Usage:\n"
            "  switchblade-analyze <in.wav> [out.json] [options]\n"
            "\n"
            "Options:\n"
            "  --mode=<auto|percussive|melodic|texture>   (default: auto)\n"
            "  --sensitivity=<float>                      detector gain (0.3..3.0, default 1.0)\n"
            "  --min-spacing=<ms>                         minimum onset spacing (default 40)\n"
            "  --snap=<ms>                                zero-cross snap radius (default 5)\n"
            "  --pretty                                   pretty-print JSON (default on)\n"
            "  --compact                                  single-line JSON\n"
            "  -h, --help                                 show this help\n";
    }

    struct Options
    {
        std::filesystem::path                       inPath;
        std::optional<std::filesystem::path>        outPath;
        switchblade::analysis::AnalysisMode         mode   { switchblade::analysis::AnalysisMode::Auto };
        switchblade::analysis::TransientDetector::Params params;
        bool                                        pretty { true };
    };

    [[nodiscard]] std::optional<switchblade::analysis::AnalysisMode>
    parseMode (std::string_view v)
    {
        using switchblade::analysis::AnalysisMode;
        if (v == "auto")        return AnalysisMode::Auto;
        if (v == "percussive")  return AnalysisMode::Percussive;
        if (v == "melodic")     return AnalysisMode::Melodic;
        if (v == "texture")     return AnalysisMode::Texture;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Options> parseArgs (int argc, char** argv)
    {
        Options o;
        std::vector<std::string_view> positional;
        positional.reserve (static_cast<std::size_t> (argc));

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a { argv[i] };
            if (a == "-h" || a == "--help")   return std::nullopt;
            if (a == "--pretty")              { o.pretty = true;  continue; }
            if (a == "--compact")             { o.pretty = false; continue; }

            if (a.starts_with ("--mode="))
            {
                const auto m = parseMode (a.substr (7));
                if (! m.has_value())
                {
                    std::cerr << "error: unknown mode '" << a.substr (7) << "'\n";
                    return std::nullopt;
                }
                o.mode = *m;
                continue;
            }
            if (a.starts_with ("--sensitivity="))
            {
                o.params.sensitivity = std::stof (std::string (a.substr (14)));
                continue;
            }
            if (a.starts_with ("--min-spacing="))
            {
                o.params.minSpacingMs = std::stof (std::string (a.substr (14)));
                continue;
            }
            if (a.starts_with ("--snap="))
            {
                o.params.zeroSnapMs = std::stof (std::string (a.substr (7)));
                continue;
            }
            if (a.starts_with ("--"))
            {
                std::cerr << "error: unknown option '" << a << "'\n";
                return std::nullopt;
            }
            positional.push_back (a);
        }

        if (positional.empty())
        {
            std::cerr << "error: missing input file\n";
            return std::nullopt;
        }
        o.inPath = std::filesystem::path (positional[0]);
        if (positional.size() >= 2)
            o.outPath = std::filesystem::path (positional[1]);
        return o;
    }
} // namespace

int main (int argc, char** argv)
{
    // JUCE requires a ScopedJuceInitialiser_GUI for MessageManager / CoreFoundation
    // on macOS, but for a pure-Core CLI the basic initialiser is enough.
    const juce::ScopedJuceInitialiser_GUI init;

    auto opts = parseArgs (argc, argv);
    if (! opts.has_value())
    {
        printUsage (std::cerr);
        return 1;
    }

    if (! std::filesystem::exists (opts->inPath))
    {
        std::cerr << "error: file not found: " << opts->inPath.string() << "\n";
        return 2;
    }

    auto result = switchblade::analysis::AnalysisEngine::analyzeSync (
        opts->inPath, opts->mode, opts->params);

    if (! result.ok())
    {
        std::cerr << "analysis failed: " << result.errorMessage << "\n";
        return 2;
    }

    const auto json = result.toJson (opts->pretty ? 2 : 0);

    if (opts->outPath.has_value())
    {
        juce::File outFile { juce::String (opts->outPath->string()) };
        if (! outFile.replaceWithText (json))
        {
            std::cerr << "error: failed to write " << opts->outPath->string() << "\n";
            return 2;
        }
        std::cerr << "ok  " << result.transients.size() << " transients -> "
                  << opts->outPath->string() << "\n";
    }
    else
    {
        std::cout << json << std::endl;
    }
    return 0;
}
