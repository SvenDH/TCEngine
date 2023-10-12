#include "private_types.h"

#include "bstrlib.h"
#include "stb_ds.h"

#include "tinyimageformat_base.h"
#include "tinyimageformat_query.h"
#include "tinyimageformat_bits.h"
#include "tinyimageformat_apis.h"


#define FS_MAX_PATH 512


static const char* get_renderer_API_name()
{
	switch (selected_api) {
#if defined(VULKAN)
	case RENDERER_VULKAN: return "VULKAN";
#endif
	default: 
	TC_ASSERT(false && "Renderer API name not defined");
	return "";
	}
}


#if defined(VULKAN)
void vk_compileShader(
	renderer_t* r, shadertarget_t target, shaderstage_t stage, const char* filename, const char* outfile, uint32_t macrocount,
	shadermacro_t* macros, binaryshaderstagedesc_t out, const char* entrypoint)
{
	bstring line = bempty();
	balloc(&line, 512);
	char filePath[FS_MAX_PATH] = { 0 };
	fsAppendPathComponent(fsGetResourceDirectory(RD_SHADER_SOURCES), filename, filePath);
	char outfilePath[FS_MAX_PATH] = { 0 };
	fsAppendPathComponent(fsGetResourceDirectory(RD_SHADER_BINARIES), outfile, outfilePath);

	bformata(&line, "-V \"%s\" -o \"%s\"", filePath, outfilePath);
	if (target >= shader_target_6_0) bcatliteral(&line, " --target-env vulkan1.1 ");
	if (target >= shader_target_6_3) bcatliteral(&line, " --target-env spirv1.4");
	if (entrypoint != NULL) bformata(&line, " -e %s", entrypoint);

		// Add platform macro
#ifdef _WINDOWS
	bcatliteral(&line, " \"-D WINDOWS\"");
#elif defined(__ANDROID__)
	bcatliteral(&line, " \"-D ANDROID\"");
#elif defined(__linux__)
	bcatliteral(&line, " \"-D LINUX\"");
#endif
	// Add user defined macros to the command line
	for (uint32_t i = 0; i < macrocount; i++)
		bformata(&line, " \"-D%s=%s\"", macros[i].definition, macros[i].value);

	const char* vulkanSdkStr = getenv("VULKAN_SDK");
	char* glslangValidator = NULL;
	if (vulkanSdkStr) {
		glslangValidator = (char*)tf_calloc(strlen(vulkanSdkStr) + 64, sizeof(char));
		strcpy(glslangValidator, vulkanSdkStr);
		strcat(glslangValidator, "/bin/glslangValidator");
	}
	else {
		glslangValidator = (char*)tf_calloc(64, sizeof(char));
		strcpy(glslangValidator, "/usr/bin/glslangValidator");
	}

	const char* args[1] = { (const char*)line.data };
	char logFileName[FS_MAX_PATH] = { 0 };
	fsGetPathFileName(outfile, logFileName);
	strcat(logFileName, "_compile.log");
	char logFilePath[FS_MAX_PATH] = { 0 };
	fsAppendPathComponent(fsGetResourceDirectory(RD_SHADER_BINARIES), logFileName, logFilePath);

	if (systemRun(glslangValidator, args, 1, logFilePath) == 0) {
		FileStream fh = {};
		bool success = fsOpenStreamFromPath(RD_SHADER_BINARIES, outfile, FM_READ_BINARY, NULL, &fh);
		//Check if the File Handle exists
		TC_ASSERT(success);
		out->mByteCodeSize = (uint32_t)fsGetStreamFileSize(&fh);
		out->pByteCode = allocShaderByteCode(pShaderByteCodeBuffer, 1, out->mByteCodeSize, filename);
		fsReadFromStream(&fh, out->pByteCode, out->mByteCodeSize);
		fsCloseStream(&fh);
	}
	else {
		FileStream fh = {};
		// If for some reason the error file could not be created just log error msg
		if (!fsOpenStreamFromPath(RD_SHADER_BINARIES, logFileName, FM_READ_BINARY, NULL, &fh))
			TRACE(LOG_ERROR, "Failed to compile shader %s", filePath);
		else {
			size_t size = fsGetStreamFileSize(&fh);
			if (size) {
				char* errorLog = (char*)tc_malloc(size + 1);
				errorLog[size] = 0;
				fsReadFromStream(&fh, errorLog, size);
				TRACE(LOG_ERROR, "Failed to compile shader %s with error\n%s", filePath, errorLog);
			}
			fsCloseStream(&fh);
		}
	}
	bdestroy(&line);
	tc_free(glslangValidator);
}
#endif

bool load_shader_stage_byte_code(
	renderer_t* r, shadertarget_t target, shaderstage_t stage, shaderstage_t allStages, const shaderstageloaddesc_t* desc,
	uint32_t macrocount, shadermacro_t* macros, binaryshaderstagedesc_t* out, shaderByteCodeBuffer* pShaderByteCodeBuffer, FSLMetadata* outMetadata)
{
	const char* rendererApi = get_renderer_API_name();

	bstring* cleanupBstrings[4] = {NULL};
	int cleanupBstringsCount = 0;
	auto cleanup = [&cleanupBstrings, &cleanupBstringsCount]() {
		for (int i = 0; i < cleanupBstringsCount; ++i)
			bdestroy(cleanupBstrings[i]);
	};

	bstring code = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &code;

	unsigned char fileNameAPIBuf[FS_MAX_PATH];
	bstring fileNameAPI = bemptyfromarr(fileNameAPIBuf);
	cleanupBstrings[cleanupBstringsCount++] = &fileNameAPI;

	if (rendererApi[0] != '\0')
		bformat(&fileNameAPI, "%s/%s", rendererApi, desc->pFileName);
	else
		bcatcstr(&fileNameAPI, desc->fileName);

	// If there are no macros specified there's no change to the shader source, we can use the binary compiled by FSL offline.
	if (macrocount == 0) {
		loadByteCode(r, RD_SHADER_BINARIES, (const char*)fileNameAPI.data, out, pShaderByteCodeBuffer, outMetadata);
		cleanup();
		return true;
	}
	TRACE(LOG_INFO, "Compiling shader in runtime: %s -> '%s' macrocount=%u", rendererApi, desc->filename, macrocount);

	time_t timeStamp = 0;
	FileStream sourceFileStream = {};
	bool sourceExists = fsOpenStreamFromPath(RD_SHADER_SOURCES, (const char*)fileNameAPI.data, FM_READ_BINARY, NULL, &sourceFileStream);
	ASSERT(sourceExists && "No source shader present for file");

	if (!loadShaderSourceFile(r->pName, &sourceFileStream, (const char*)fileNameAPI.data, &timeStamp, &code)) {
		fsCloseStream(&sourceFileStream);
		cleanup();
		return false;
	}

	bstring shaderDefines = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &shaderDefines;

	balloc(&shaderDefines, 64);

	// Apply user specified macros
	for (uint32_t i = 0; i < macrocount; i++) {
		bcatcstr(&shaderDefines, macros[i].definition);
		bcatcstr(&shaderDefines, macros[i].value);
	}

	char extension[FS_MAX_PATH] = { 0 };
	fsGetPathExtension(desc->pFileName, extension);
	char fileName[FS_MAX_PATH] = { 0 };
	fsGetPathFileName(desc->pFileName, fileName);

	bstring binaryShaderComponent = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &binaryShaderComponent;

	balloc(&binaryShaderComponent, 128);

	static const size_t seed = 0x31415926;
	size_t shaderDefinesHash = stbds_hash_bstring(&shaderDefines, seed);
	bformat(&binaryShaderComponent, "%s_%s_%zu_%s_%u", rendererApi, fileName, shaderDefinesHash, extension, target);

	bcatliteral(&binaryShaderComponent, ".bin");

	// Shader source is newer than binary
	if (!check_for_byte_code(r, (const char*)binaryShaderComponent.data, timeStamp, out, pShaderByteCodeBuffer)) {
		switch (selected_api) {
#if defined(VULKAN)
			case RENDERER_API_VULKAN:
#if defined(__ANDROID__)
				vk_compileShader(
					r, stage, (uint32_t)code.slen, (const char*)code.data, (const char*)binaryShaderComponent.data, macrocount, macros, out,
					pShaderByteCodeBuffer, desc->entrypointName);
				if (!save_byte_code((const char*)binaryShaderComponent.data, (char*)(out->bytecode), out->bytecodesize))
					LOGF(LogLevel::eWARNING, "Failed to save byte code for file %s", desc->filename);
#else
				vk_compileShader(
					r, target, stage, (const char*)fileNameAPI.data, (const char*)binaryShaderComponent.data, macrocount, macros, out,
					pShaderByteCodeBuffer, desc->entrypointName);
#endif
				break;
#endif
			default: break;
		}
		if (!out->pByteCode) {
			TRACE(LOG_ERROR, "Error while generating bytecode for shader %s", desc->pFileName);
			fsCloseStream(&sourceFileStream);
			TC_ASSERT(false);
			cleanup();
			return false;
		}
	}

	fsCloseStream(&sourceFileStream);
	cleanup();
	return true;
}

void load_shader(renderer_t* r, const shaderloaddesc_t* desc, shader_t* shader)
{
	if ((uint32_t)desc->target > r->shadertarget) {
		TRACE(LOG_ERROR, 
			"Requested shader target (%u) is higher than the shader target that the renderer supports (%u). Shader wont be compiled",
			(uint32_t)desc->target, (uint32_t)r->shadertarget
		);
		return;
	}
	binaryshaderdesc_t bindesc = { 0 };
	shaderstage_t stages = SHADER_STAGE_NONE;
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		if (desc->stages[i].filename && desc->stages[i].filename[0] != 0) {
			shaderstage_t stage;
			binaryshaderstagedesc_t* stage = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fsGetPathExtension(desc->stages[i].filename, ext);
			if (find_shader_stage(ext, &bindesc, &pStage, &stage))
				stages |= stage;
		}
	}
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		if (desc->stages[i].pFileName && desc->mStages[i].pFileName[0] != 0) {
			const char* fileName = desc->mStages[i].pFileName;
			shaderstage_t stage;
			binaryshaderstagedesc_t* pStage = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fsGetPathExtension(fileName, ext);
			if (find_shader_stage(ext, &bindesc, &pStage, &stage)) {
				combinedFlags |= desc->mStages[i].mFlags;
				uint32_t macrocount = desc->mStages[i].mMacroCount;
				
				shadermacro_t* macros = NULL;
				arrsetlen(macros, macrocount);
				for (uint32_t macro = 0; macro < desc->mStages[i].mMacroCount; ++macro)
					macros[macro] = desc->mStages[i].macros[macro]; //-V595

				FSLMetadata metadata = {};
				if (!load_shader_stage_byte_code(r, desc->target, stage, stages, desc->stages[i], macrocount, macros, pStage, &shaderByteCodeBuffer, &metadata)) {
					arrfree(macros);
					freeShaderByteCode(&shaderByteCodeBuffer, &bindesc);
					return;
				}
				arrfree(macros);

				bindesc.mStages |= stage;
				
				if (desc->mStages[i].entrypointName)
					pStage->entrypoint = desc->mStages[i].entrypointName;
				else
					pStage->entrypoint = "main";
			}
		}
	}
	bindesc.mConstantCount = desc->mConstantCount;
	bindesc.pConstants = desc->pConstants;

	add_shaderbinary(r, &bindesc, shader);
	freeShaderByteCode(&shaderByteCodeBuffer, &bindesc);
}
