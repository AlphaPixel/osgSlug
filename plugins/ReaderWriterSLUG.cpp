#include "osgSlug/Atlas.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osgDB/ReaderWriter>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <osgDB/ReadFile>

OSGSLUG_ENABLE_WARNINGS

#define EXTENSION_NAME "slug"

class ReaderWriterSLUG: public osgDB::ReaderWriter {
public:
	ReaderWriterSLUG() {
		supportsExtension(EXTENSION_NAME, "osgSlug loader");
	}

	const char* className() const override { return "osgSlug loader"; }

	bool acceptsExtension(const std::string& extension) const override {
		return osgDB::equalCaseInsensitive(extension, EXTENSION_NAME);
	}

	ReadResult readObject(
		std::istream& fin,
		const osgDB::ReaderWriter::Options* options=nullptr
	) const override {
		return ReadResult::FILE_NOT_HANDLED;
	}

	ReadResult readObject(
		const std::string& file,
		const osgDB::ReaderWriter::Options* options=nullptr
	) const override {
		std::string ext = osgDB::getLowerCaseFileExtension(file);

		if(!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

		std::string fileName = osgDB::findDataFile(file, options);

		if(fileName.empty()) return ReadResult::FILE_NOT_FOUND;

		osgDB::ifstream istream(fileName.c_str(), std::ios::in | std::ios::binary);

		if(!istream) return ReadResult::FILE_NOT_HANDLED;

		return ReadResult::FILE_NOT_HANDLED;

		/* ReadResult rr = readRGBStream(istream);

		if(rr.validObject()) rr.getObject()->setFileName(file);

		return rr; */
	}

	WriteResult writeObject(
		const osg::Object& obj,
		std::ostream& fout,
		const osgDB::ReaderWriter::Options* options
	) const override {
		return WriteResult::ERROR_IN_WRITING_FILE;
	}

	WriteResult writeObject(
		const osg::Object &obj,
		const std::string& fileName,
		const osgDB::ReaderWriter::Options* options
	) const override {
		std::string ext = osgDB::getFileExtension(fileName);

		if(!acceptsExtension(ext)) return WriteResult::FILE_NOT_HANDLED;

		osgDB::ofstream fout(fileName.c_str(), std::ios::out | std::ios::binary);

		if(!fout) return WriteResult::ERROR_IN_WRITING_FILE;

		return writeObject(obj, fout, options);
	}
};

// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(slug, ReaderWriterSLUG)
