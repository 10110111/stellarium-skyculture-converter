#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QCoreApplication>
#include <QRegularExpression>
#include "NamesOldLoader.hpp"
#include "AsterismOldLoader.hpp"
#include "DescriptionOldLoader.hpp"
#include "ConstellationOldLoader.hpp"

QString convertLicense(const QString& license)
{
	auto parts = license.split("+");
	for(auto& p : parts)
		p = p.simplified();
	for(auto& lic : parts)
	{
		if(lic.startsWith("Free Art ")) continue;

		lic.replace(QRegularExpression("(?: International)?(?: Publice?)? License"), "");
	}

	if(parts.size() == 1) return parts[0];
	if(parts.size() == 2)
	{
		if(parts[1].startsWith("Free Art ") && !parts[0].startsWith("Free Art "))
			return "Text and data: " + parts[0] + "\n\nIllustrations: " + parts[1];
		else if(parts[0].startsWith("Free Art ") && !parts[1].startsWith("Free Art "))
			return "Text and data: " + parts[1] + "\n\nIllustrations: " + parts[0];
	}
	std::cerr << "Unexpected combination of licenses, leaving them unformatted.\n";
	return license;
}

void convertInfoIni(const QString& dir, std::ostream& s, QString& boundariesType, QString& author, QString& credit, QString& license, QString& cultureId, QString& region, QString& englishName)
{
	QSettings pd(dir + "/info.ini", QSettings::IniFormat); // FIXME: do we really need StelIniFormat here instead?
	englishName = pd.value("info/name").toString();
	author = pd.value("info/author").toString();
	credit = pd.value("info/credit").toString();
	license = pd.value("info/license", "").toString();
	region = pd.value("info/region", "???").toString();
	const auto classification = pd.value("info/classification").toString();
	boundariesType = pd.value("info/boundaries", "none").toString();

	cultureId = QFileInfo(dir).fileName();

	// Now emit the JSON snippet
	s << "{\n"
	   "  \"id\": \"" + cultureId.toStdString() + "\",\n"
	   "  \"region\": \"" + region.toStdString() + "\",\n"
	   "  \"classification\": [\"" + classification.toStdString() + "\"],\n"
	   "  \"fallback_to_international_names\": false,\n";
}

void writeEnding(std::string& s)
{
	if(s.size() > 2 && s.substr(s.size() - 2) == ",\n")
		s.resize(s.size() - 2);
	s += "\n}\n";
}

int usage(const char* argv0, const int ret)
{
	auto& out = ret ? std::cerr : std::cout;
	out << "Usage: " << argv0 << " [options...] skyCultureDir outputDir [skyCulturePoDir]\n"
	    << "Options:\n"
	    << "  --footnotes-to-references  Try to convert footnotes to references\n"
	    << "  --untrans-names-are-native Record untranslatable star/DSO names as native names\n"
	    << "  --native-locale LOCALE     Use *_names.LOCALE.fab as a source for \"native\" constellation names (the\n"
	       "                             middle column in *_names.eng.fab will be moved to the \"pronounce\" entry.\n"
	    << "  --translated-md            Generate localized Markdown files (for checking translations)\n";
	return ret;
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	QString inDir;
	QString outDir;
	QString poDir;
	QString nativeLocale;
	bool footnotesToRefs = false, genTranslatedMD = false;
	bool convertUntranslatableNamesToNative = false;
	for(int n = 1; n < argc; ++n)
	{
		const std::string arg = argv[n];
		if(arg.size() >= 1 && arg[0] != '-')
		{
			if(inDir.isEmpty()) inDir = arg.c_str();
			else if(outDir.isEmpty()) outDir = arg.c_str();
			else if(poDir.isEmpty()) poDir = arg.c_str();
			else
			{
				std::cerr << "Unknown command-line parameter: \"" << arg << "\"\n";
				return usage(argv[0], 1);
			}
		}
		else if(arg == "--translated-md")
			genTranslatedMD = true;
		else if(arg == "--footnotes-to-references")
			footnotesToRefs = true;
		else if(arg == "--untrans-names-are-native")
			convertUntranslatableNamesToNative = true;
		else if(arg == "--native-locale")
		{
			++n;
			if(n == argc)
			{
				std::cerr << "Option \"" << arg << "\" requires argument\n";
				return usage(argv[0], 1);
			}
			nativeLocale = argv[n];
		}
		else if(arg == "--help" || arg == "-h")
			return usage(argv[0], 0);
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return usage(argv[0], 1);
        }
	}
	if(inDir.isEmpty())
	{
		std::cerr << "Input sky culture directory not specified.\n";
		return usage(argv[0], 1);
	}
	if(outDir.isEmpty())
	{
		std::cerr << "Output directory not specified.\n";
		return usage(argv[0], 1);
	}
	if(poDir.isEmpty())
		std::cerr << "Warning: translations (po) directory not specified. Will not load existing translations of names.\n";

	if(QFile(outDir).exists())
	{
		std::cerr << "Output directory already exists, won't touch it.\n";
		return 1;
	}
	if (!QFile(inDir+"/info.ini").exists())
	{
		std::cerr << "Error: info.ini file wasn't found\n";
		return 1;
	}
	inDir = QDir::fromNativeSeparators(inDir);
	// Remove trailing directory separators, so that QFileInfo::fileName
	// gave sky culture id rather than an empty string.
	while(inDir.endsWith("/")) inDir.chop(1);
	std::stringstream out;
	QString boundariesType, author, credit, license, cultureId, region, englishName;
	convertInfoIni(inDir, out, boundariesType, author, credit, license, cultureId, region, englishName);

	AsterismOldLoader aLoader;
	aLoader.load(inDir, cultureId);

	ConstellationOldLoader cLoader;
	cLoader.setBoundariesType(boundariesType.toStdString());
	cLoader.load(inDir, outDir, nativeLocale);

	NamesOldLoader nLoader;
	nLoader.load(inDir, nativeLocale, convertUntranslatableNamesToNative);

	std::cerr << "Starting emission of JSON...\n\n";
	aLoader.dumpJSON(out);
	cLoader.dumpJSON(out);
	nLoader.dumpJSON(out);

	auto str = std::move(out).str();
	writeEnding(str);

	if(!QDir().mkpath(outDir))
	{
		std::cerr << "Failed to create output directory\n";
		return 1;
	}
	{
		std::ofstream outFile((outDir+"/index.json").toStdString());
		outFile << str;
		outFile.flush();
		if(!outFile)
		{
			std::cerr << "Failed to write index.json\n";
			return 1;
		}
	}

	DescriptionOldLoader dLoader;
	license = convertLicense(license);
	dLoader.load(inDir, poDir, cultureId, englishName, author, credit, license, cLoader, aLoader, nLoader,
	             footnotesToRefs, genTranslatedMD);
	dLoader.dump(outDir);

	std::cerr << "--- NOTE ---\n";
	if(region == "???")
	{
		std::cerr << "* Some JSON values can't be deduced from the old-format data. They have been"
			     " marked by \"???\". Please replace them with something sensible.\n";
	}
	std::cerr << "* The key \"langs_use_native_names\" is omitted since it has no counterpart"
	             " in the old format. If this sky culture needs it, please add it manually.\n";
	std::cerr << "* The transformation of the description text is very basic, please check that"
	             " it looks as it should. Pay special attention at References, Authors, and"
	             " License sections, which may have been formulated in a suboptimal way.\n";
}
