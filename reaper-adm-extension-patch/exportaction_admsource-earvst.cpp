#include "exportaction_admsource-earvst.h"

#include "pluginregistry.h"
#include "pluginsuite_ear.h"
#include <version/eps_version.h>
#include <helper/adm_preset_definitions_helper.h>
#include <speaker_setups.hpp>

#include <adm/write.hpp>
#include <adm/utilities/id_assignment.hpp>
#include <adm/parse.hpp>

#include <algorithm>
#include <sstream>

#include <daw_channel_count.h>

using namespace admplug;

EarVstExportSources::EarVstExportSources(ReaperAPI const & api) : IExportSources(api)
{
    MediaTrack *trk;
    int numTracks = api.CountTracks(nullptr);

    for (int trackNum = 0; trackNum < numTracks; trackNum++) {

        trk = api.GetTrack(nullptr, trackNum);
        if (trk) {

            auto fxPosVec = EarSceneMasterVst::trackEarSceneMasterVstIndexes(api, trk);
            for(int fxPos : fxPosVec) {
                auto earSceneMasterVst = std::make_shared<EarSceneMasterVst>(trk, fxPos, api);
                auto comms = earSceneMasterVst->getCommunicator(true);
                if(comms) comms->updateInfo();

                allEarSceneMasterVsts.push_back(earSceneMasterVst);
                if(EarSceneMasterVst::isCandidateForExport(earSceneMasterVst)) {
                    if(!chosenCandidateForExport) {
                        chosenCandidateForExport = earSceneMasterVst;
                        generateAdmAndChna(api);
                    }
                    candidatesForExport.push_back(earSceneMasterVst);
                }
            }
        }
    }

    if(!chosenCandidateForExport) {
        errorStrings.push_back(std::string("No instance of EAR Scene found which is able to export!"));
    }
}

int EarVstExportSources::getSampleRate()
{
    if(chosenCandidateForExport){
        int sr = chosenCandidateForExport->getSampleRate();
        if(sr > 0) return sr;
    }
    for(auto &thisVst : allEarSceneMasterVsts) {
        int sr = thisVst->getSampleRate();
        if(sr > 0) return sr;
    }
    return 0;
}

int EarVstExportSources::getTotalExportChannels()
{
    if(chosenCandidateForExport) {
        return chosenCandidateForExport->getChannelCount();
    }
    return 0;
}

void EarVstExportSources::setRenderInProgress(bool state)
{
    if(chosenCandidateForExport){
        chosenCandidateForExport->setRenderInProgressState(true);
    }
}

bool EarVstExportSources::isFrameAvailable()
{
    if(!chosenCandidateForExport) return false;
    return chosenCandidateForExport->getCommunicator()->nextFrameAvailable();
}

bool EarVstExportSources::writeNextFrameTo(float * bufferWritePointer, bool skipFrameAvailableCheck)
{
    if(!skipFrameAvailableCheck && !isFrameAvailable()) return false;

    auto communicator = chosenCandidateForExport->getCommunicator();
    int channelsToWrite = communicator->getReportedChannelCount();
    if(channelsToWrite > 0) {
        if(!communicator->copyNextFrame(bufferWritePointer, true)) {
            throw std::runtime_error("copyNextFrame failed");
        }
        bufferWritePointer += channelsToWrite;
    }

    return true;
}

void EarVstExportSources::generateAdmAndChna(ReaperAPI const& api)
{
    using namespace adm;

    // Get ADM template in to admDocument
    std::istringstream sstr(chosenCandidateForExport->getAdmTemplateStr());
    try {
        admDocument = adm::parseXml(sstr, adm::xml::ParserOptions::recursive_node_search); // Shouldn't need recursive here, but doesn't harm to do it anyway
    } catch(std::exception &e) {
        std::string str = "Failed to parse ADM template: \"";
        str += e.what();
        str += "\"";
        errorStrings.push_back(str);
        return;
    }

    // Any track UID that won't be written should be removed
    std::vector<uint32_t> audioTrackUidIdsInChannelMappings;
    for(auto const& channelMapping : chosenCandidateForExport->getChannelMappings()) {
        for(auto const& plugin : channelMapping.plugins) {
            audioTrackUidIdsInChannelMappings.push_back(plugin.audioTrackUidVal);
        }
    }
    auto docAudioTrackUids = admDocument->getElements<adm::AudioTrackUid>();
    std::vector<std::shared_ptr<adm::AudioTrackUid>> missingList;
    for(auto docAudioTrackUid : docAudioTrackUids) {
        uint32_t idValue = docAudioTrackUid->get<adm::AudioTrackUidId>().get<adm::AudioTrackUidIdValue>().get();
        if(std::find(audioTrackUidIdsInChannelMappings.begin(), audioTrackUidIdsInChannelMappings.end(), idValue) == audioTrackUidIdsInChannelMappings.end()) {
            missingList.push_back(docAudioTrackUid);
        }
    }
    for(auto uid : missingList) {
        auto tf = uid->getReference<adm::AudioTrackFormat>();
        if(tf) {
            auto sf = tf->getReference<adm::AudioStreamFormat>();
            if(sf) {
                admDocument->remove(sf);
            }
            admDocument->remove(tf);
        }
        admDocument->remove(uid);
    }

    // Create CHNA chunk
    std::vector<bw64::AudioId> audioIds;

    for(auto const& channelMapping : chosenCandidateForExport->getChannelMappings()) {
        for(auto const& plugin : channelMapping.plugins) {
            auto admElements = getAdmElementsFor(plugin);
            assert(admElements.has_value());

            if(admElements->audioTrackFormat) {
                // BS.2076-1 style structure (ATUID->ATF->ASF->ACF)
                audioIds.push_back(bw64::AudioId(channelMapping.writtenChannelNumber + 1, //1-Indexed in CHNA!!!!!!!!!!!!!!!
                                                 formatId((*admElements).audioTrackUid->get<AudioTrackUidId>()),
                                                 formatId((*admElements).audioTrackFormat->get<AudioTrackFormatId>()),
                                                 formatId((*admElements).audioPackFormat->get<AudioPackFormatId>())
                ));
            } else {
                // BS.2076-2 style structure (ATUID->ACF)
                std::string cfId = formatId(admElements->audioChannelFormat->get<AudioChannelFormatId>());
                cfId += "_00";
                audioIds.push_back(bw64::AudioId(channelMapping.writtenChannelNumber + 1, //1-Indexed in CHNA!!!!!!!!!!!!!!!
                                                 formatId((*admElements).audioTrackUid->get<AudioTrackUidId>()),
                                                 cfId,
                                                 formatId((*admElements).audioPackFormat->get<AudioPackFormatId>())
                ));
            }
        }
    }

    chnaChunk = std::make_shared<bw64::ChnaChunk>(bw64::ChnaChunk(audioIds));

    //Populate rest of ADM template (admDocument) here with AudioBlockFormats

    std::shared_ptr<admplug::PluginSuite> pluginSuite = std::make_shared<EARPluginSuite>();

    auto channelMappings = chosenCandidateForExport->getChannelMappings();
    for(auto const& channelMapping : channelMappings) {
        for(auto const& plugin : channelMapping.plugins) {

            auto admElements = getAdmElementsFor(plugin);
            assert(admElements.has_value());

            // Find the associated plugin

            auto feedingPlugins = getEarInputPluginsWithInputInstanceId(plugin.inputInstanceId, api);
            if(feedingPlugins.size() == 0) {
                std::string msg("Unable to find Input plugin with instance ID ");
                msg += std::to_string(plugin.inputInstanceId);
                errorStrings.push_back(msg);
                continue;
            } else if(feedingPlugins.size() > 1) {
                std::string msg("Multiple Input plugins found with instance ID ");
                msg += std::to_string(plugin.inputInstanceId);
                errorStrings.push_back(msg);
                continue;
            }
            auto pluginInst = feedingPlugins[0];

            // Set audioobject start/duration

            auto start = std::chrono::nanoseconds::zero();
            auto duration = toNs(api.GetProjectLength(nullptr));

            if(std::stof(api.GetAppVersion()) >= 6.01f)
            {
                // Check if Tail option in rendering dialog is activated and add length to duration if so
                bool tailFlag = static_cast<size_t>(api.GetSetProjectInfo(nullptr, "RENDER_TAILFLAG", 0., false)) & 0x2;
                bool boundsFlag = static_cast<size_t>(api.GetSetProjectInfo(nullptr, "RENDER_BOUNDSFLAG", 0., false)) == 1;
                if(tailFlag && boundsFlag) {
                    const double tailLengthMs = api.GetSetProjectInfo(nullptr, "RENDER_TAILMS", 0., false);
                    duration += toNs(tailLengthMs / 1000.);
                }
            }

            auto mediaTrack = pluginInst->getTrackInstance().get();
            auto bounds = api.getTrackAudioBounds(mediaTrack, true); // True = ignore before zero - we don't do sub-zero bounds
            if(bounds.has_value()) {
                start = toNs(bounds->first);
                duration = toNs(bounds->second - bounds->first);
                admElements->audioObject->set(adm::Start{ start });
                admElements->audioObject->set(adm::Duration{ duration });
            }

            const bool isPresetDef = AdmPresetDefinitionsHelper::getSingleton().getPackFormatData(admElements->audioPackFormat) != nullptr;
            const bool isObjectType = admElements->typeDescriptor == adm::TypeDefinition::OBJECTS;

            if (!isPresetDef) {
                if (isObjectType) {
                    // Need to generate BFs
                    auto cumulatedPointData = CumulatedPointData(start, start + duration);

                    // Get all values for all parameters, whether automated or not.
                    for (int admParameterIndex = 0; admParameterIndex != (int)AdmParameter::NONE; admParameterIndex++) {
                        auto admParameter = (AdmParameter)admParameterIndex;
                        auto param = pluginSuite->getParameterFor(admParameter);
                        auto env = getEnvelopeFor(pluginSuite, pluginInst.get(), admParameter, api);

                        if (getEnvelopeBypassed(env, api)) {
                            // We have an envelope, but it is bypassed
                            auto val = getValueFor(pluginSuite, pluginInst.get(), admParameter, api);
                            auto newErrors = cumulatedPointData.useConstantValueForParameter(admParameter, *val);
                            for (auto& newError : newErrors) {
                                warningStrings.push_back(newError.what());
                            }

                        } else if (param && env) {
                            // We have an envelope for this ADM parameter
                            auto newErrors = cumulatedPointData.useEnvelopeDataForParameter(*env, *param, admParameter, api);
                            for (auto& newError : newErrors) {
                                warningStrings.push_back(newError.what());
                            }

                        } else if (auto val = getValueFor(pluginSuite, pluginInst.get(), admParameter, api)) {
                            // We do not have an envelope for this ADM parameter but the plugin suite CAN provide a fixed value for it
                            // NOTE that this will include parameters NOT relevant to the current audioObject type, but these are ignored during block creation.
                            auto newErrors = cumulatedPointData.useConstantValueForParameter(admParameter, *val);
                            for (auto& newError : newErrors) {
                                warningStrings.push_back(newError.what());
                            }
                        }
                    }

                    auto blocks = cumulatedPointData.generateAudioBlockFormatObjects(pluginSuite, pluginInst.get(), api);
                    for (auto& block : *blocks) admElements->audioChannelFormat->add(*block);

                }
                else {
                    warningStrings.push_back("Currently only supporting Preset Definitions for non-Objects types");
                }
            }
        }
    }

    // Create AXML Chunk
    std::stringstream xmlStream;
    adm::writeXml(xmlStream, admDocument);
    xmlStream << "<!-- Produced using the EAR Production Suite (version ";
    xmlStream << (eps::versionInfoAvailable()? eps::currentVersion() : "unknown");
    xmlStream << "), from the EAR Scene plugin -->\n";
    auto xmlStr = xmlStream.str();
    axmlChunk = std::make_shared<bw64::AxmlChunk>(bw64::AxmlChunk(xmlStr));

}

std::optional<EarVstExportSources::AdmElements> EarVstExportSources::getAdmElementsFor(const PluginToAdmMap& plugin)
{
    auto audioObjectId = adm::AudioObjectId(adm::AudioObjectIdValue(plugin.audioObjectIdVal));
    auto audioObject = admDocument->lookup(audioObjectId);
    if(!audioObject) {
        auto msg = std::string("Audio Object with ID ");
        msg += adm::formatId(audioObjectId);
        msg += " not found for Input Instance ID ";
        msg += std::to_string(plugin.inputInstanceId);
        warningStrings.push_back(msg);
        return std::optional<AdmElements>();
    }

    auto audioTrackUidId = adm::AudioTrackUidId(adm::AudioTrackUidIdValue(plugin.audioTrackUidVal));
    auto audioTrackUid = admDocument->lookup(audioTrackUidId);
    if(!audioTrackUid) {
        auto msg = std::string("Audio Track UID with ID ");
        msg += adm::formatId(audioTrackUidId);
        msg += " not found for Input Instance ID ";
        msg += std::to_string(plugin.inputInstanceId);
        warningStrings.push_back(msg);
        return std::optional<AdmElements>();
    }

    // We make a lot of assumptions about the ADM structure here, but since we know what the EAR plugins will send us, this is probably safe

    auto audioPackFormat = audioTrackUid->getReference<adm::AudioPackFormat>();
    assert(audioPackFormat);
    if(!audioPackFormat) {
        warningStrings.push_back(std::string("No Pack Format referenced from ") + adm::formatId(audioTrackUidId));
        return std::optional<AdmElements>();
    }

    auto audioTrackFormat = audioTrackUid->getReference<adm::AudioTrackFormat>();
    auto audioChannelFormat = audioTrackUid->getReference<adm::AudioChannelFormat>();
    assert(audioTrackFormat || audioChannelFormat);
    if(!audioTrackFormat && !audioChannelFormat) {
        warningStrings.push_back(std::string("No Track Format or Channel Format referenced from ") + adm::formatId(audioTrackUidId));
        return std::optional<AdmElements>();
    }

    if(audioTrackFormat) {
        auto audioStreamFormat = audioTrackFormat->getReference<adm::AudioStreamFormat>();
        assert(audioStreamFormat);
        if(!audioStreamFormat) {
            warningStrings.push_back(std::string("No Stream Format referenced recursively from ") + adm::formatId(audioTrackUidId));
            return std::optional<AdmElements>();
        }

        audioChannelFormat = audioStreamFormat->getReference<adm::AudioChannelFormat>();
        assert(audioChannelFormat);
        if(!audioChannelFormat) {
            warningStrings.push_back(std::string("No Channel Format referenced recursively from ") + adm::formatId(audioTrackUidId));
            return std::optional<AdmElements>();
        }
    }

    auto audioChannelFormatId = audioChannelFormat->get<adm::AudioChannelFormatId>().get<adm::AudioChannelFormatIdValue>().get();
    adm::TypeDescriptor typeDescriptor = audioChannelFormat->get<adm::TypeDescriptor>();

    return AdmElements{
        typeDescriptor,
        audioTrackUid,
        audioTrackFormat,
        audioPackFormat,
        audioChannelFormat,
        audioObject
    };

}

namespace {

// Field order must match reverb_plugin_processor.cpp's own per-point
// addParameter() loop exactly (azimuth, elevation, distance, gain, width,
// height, depth, diffuse).
int reverbPointFieldOffsetFor(AdmParameter admParameter) {
    switch (admParameter) {
        case AdmParameter::OBJECT_AZIMUTH: return 0;
        case AdmParameter::OBJECT_ELEVATION: return 1;
        case AdmParameter::OBJECT_DISTANCE: return 2;
        case AdmParameter::OBJECT_GAIN: return 3;
        case AdmParameter::OBJECT_WIDTH: return 4;
        case AdmParameter::OBJECT_HEIGHT: return 5;
        case AdmParameter::OBJECT_DEPTH: return 6;
        case AdmParameter::OBJECT_DIFFUSE: return 7;
        default: return -1;
    }
}

// Wraps a PluginParameter (as returned by EARPluginSuite::getPluginParameterFor(),
// which always assumes one plugin instance = one object, at a single fixed
// index) and redirects index() to one of "LF Reverb"'s own
// per-point parameters instead - everything else (forwardMap/reverseMap/
// admParameter) is identical between a point and a real Object, so those
// just forward to the wrapped parameter unchanged.
class ShiftedPluginParameter : public PluginParameter {
public:
    ShiftedPluginParameter(PluginParameter const& base, int shiftedIndex)
        : base_(base), shiftedIndex_(shiftedIndex) {}
    int index() const override { return shiftedIndex_; }
    double forwardMap(double val) const override { return base_.forwardMap(val); }
    double reverseMap(double val) const override { return base_.reverseMap(val); }
    AdmParameter admParameter() const override { return base_.admParameter(); }

private:
    PluginParameter const& base_;
    int shiftedIndex_;
};

// If pluginInst is a matched point of a multi-object "Reverb for EAR
// Suite" instance, and admParameter has a per-point equivalent, returns a
// parameter pointing at that point's own raw index instead of param's
// original (single-object) one. Otherwise returns nullptr (caller keeps
// using the original param).
std::unique_ptr<PluginParameter> resolveForPoint(PluginInstance* pluginInst,
                                                 AdmParameter admParameter,
                                                 PluginParameter const& param) {
    auto* reverbInst = dynamic_cast<EarInputVst*>(pluginInst);
    if (reverbInst && reverbInst->isMultiObjectPlugin() && reverbInst->matchedPointIndex() >= 0) {
        int fieldOffset = reverbPointFieldOffsetFor(admParameter);
        if (fieldOffset >= 0) {
            int shiftedIndex = reverbInst->pointParamBaseIndex() +
                               reverbInst->matchedPointIndex() * 8 + fieldOffset;
            return std::make_unique<ShiftedPluginParameter>(param, shiftedIndex);
        }
    }
    return nullptr;
}

}  // namespace

TrackEnvelope* EarVstExportSources::getEnvelopeFor(std::shared_ptr<admplug::PluginSuite> pluginSuite, PluginInstance * pluginInst, AdmParameter admParameter, ReaperAPI const & api)
{
    MediaTrack* track = pluginInst->getTrackInstance().get();

    // Plugin Parameters
    if(auto param = pluginSuite->getPluginParameterFor(admParameter)) {
        if (auto shifted = resolveForPoint(pluginInst, admParameter, *param)) {
            return api.GetFXEnvelope(track, pluginInst->getPluginIndex(), shifted->index(), false);
        }
        return api.GetFXEnvelope(track, pluginInst->getPluginIndex(), param->index(), false);
    }

    return nullptr;
}

std::optional<double> EarVstExportSources::getValueFor(std::shared_ptr<admplug::PluginSuite> pluginSuite, PluginInstance * pluginInst, AdmParameter admParameter, ReaperAPI const & api)
{
    MediaTrack* track = pluginInst->getTrackInstance().get();

    // Plugin Parameters
    if(auto param = pluginSuite->getPluginParameterFor(admParameter)) {
        if (auto shifted = resolveForPoint(pluginInst, admParameter, *param)) {
            return pluginInst->getParameterWithConvert(*shifted);
        }
        return pluginInst->getParameterWithConvert(*param);
    }

    return std::optional<double>();
}

bool EarVstExportSources::getEnvelopeBypassed(TrackEnvelope* env, ReaperAPI const& api)
{
    bool envBypassed = false;
    if(env) {
        char chunk[1024]; // For a plugin parameter (PARMENV) the ACT flag should always be within the first couple bytes of the state chunk
        bool getRes = api.GetEnvelopeStateChunk(env, chunk, 1024, false);
        if (getRes) {
            std::istringstream chunkSs(chunk);
            std::string line;
            while (std::getline(chunkSs, line)) {
                auto activePos = line.rfind("ACT ", 0);
                if ((activePos != std::string::npos) && (line.size() > (activePos + 4))) {
                    envBypassed = line.at(activePos + 4) == '0';
                    break;
                }
            }
        }
    }
    return envBypassed;
}

std::vector<std::shared_ptr<PluginInstance>> EarVstExportSources::getEarInputPluginsWithInputInstanceId(uint32_t inputInstanceId, ReaperAPI const& api)
{
    std::vector<std::shared_ptr<PluginInstance>> insts;

    for(int trackNum = 0; trackNum < api.CountTracks(0); trackNum++) {
        auto trk = api.GetTrack(0, trackNum);
        for(int vstPos = 0; vstPos < api.TrackFX_GetCount(trk); vstPos++) {
            if(EarInputVst::isInputPlugin(api, trk, vstPos)) {
                auto pluginInst = std::make_shared<EarInputVst>(trk, vstPos, api);

                if(pluginInst->matchesInputInstanceId(inputInstanceId)) {
                    // Push pluginInst itself, not a fresh plain
                    // PluginInstance(trk, vstPos, api) copy as before - for
                    // a multi-object Reverb match, matchedPointIndex() lives
                    // on this exact EarInputVst instance and would
                    // otherwise be lost (see getEnvelopeFor()/getValueFor()
                    // downstream, which need it to pick the right point's
                    // position/gain parameters).
                    insts.push_back(pluginInst);
                }
            }
        }
    }
    return insts;
}

//EarInputVst

std::string EarInputVst::directSpeakersVstName = admplug::EARPluginSuite::DIRECTSPEAKERS_METADATA_PLUGIN_NAME;

const std::string* EarInputVst::getDirectSpeakersVstNameStr()
{
    return &directSpeakersVstName;
}

bool EarInputVst::isDirectSpeakersVstAvailable(ReaperAPI const& api, bool doRescan)
{
    return PluginRegistry::getInstance()->checkPluginAvailable(directSpeakersVstName, api, doRescan);
}

std::string EarInputVst::objectVstName = admplug::EARPluginSuite::OBJECT_METADATA_PLUGIN_NAME;

const std::string* EarInputVst::getObjectVstNameStr()
{
    return &objectVstName;
}

bool EarInputVst::isObjectVstAvailable(ReaperAPI const& api, bool doRescan)
{
    return PluginRegistry::getInstance()->checkPluginAvailable(objectVstName, api, doRescan);
}

bool EarInputVst::isObjectPlugin(const std::string& vstNameStr)
{
    // objectVstName ("EAR Object") is the stock EBU plugin's exact display
    // name - this is the only thing that identifies an Object input plugin
    // to the rest of this file (export instance-ID lookup, track-mapping
    // assignment, etc.), so a renamed fork of the same plugin (same state/
    // parameter layout, just a different DISPLAY_NAME/OUTPUT_NAME) would
    // otherwise be invisible to ADM export entirely - "Unable to find
    // Input plugin with instance ID ..." even though the plugin and its
    // Scene-plugin link are working fine. Recognize known renamed forks
    // here too, alongside the stock name.
    static const std::vector<std::string> recognizedObjectPluginNames = {
        objectVstName,
        "LF Object OSC", // ear-suite-extras fork (OSC remote control added)
    };
    return std::find(recognizedObjectPluginNames.begin(), recognizedObjectPluginNames.end(),
                     vstNameStr) != recognizedObjectPluginNames.end();
}

bool EarInputVst::isDirectSpeakersPlugin(const std::string& vstNameStr)
{
    // Same recognized-forks pattern as isObjectPlugin() above. "LF LFE
    // Send" mirrors the stock DirectSpeakers plugin's own parameter layout
    // exactly (routing, packFormatIdValue, bypass, useTrackName,
    // inputInstanceId at index 4 - see lfe_send_plugin_processor.cpp and
    // paramDirectSpeakersInstanceId/paramDirectSpeakersPackFormatIdValue
    // below), just restricted to a single LFE channel's pack format, so it
    // is read identically here.
    static const std::vector<std::string> recognizedDirectSpeakersPluginNames = {
        directSpeakersVstName,
        "LF LFE Send", // ear-suite-extras fork (LFE-only DirectSpeakers)
    };
    return std::find(recognizedDirectSpeakersPluginNames.begin(),
                     recognizedDirectSpeakersPluginNames.end(),
                     vstNameStr) != recognizedDirectSpeakersPluginNames.end();
}

std::string EarInputVst::reverbVstName = "LF Reverb";

const std::string* EarInputVst::getReverbVstNameStr()
{
    return &reverbVstName;
}

bool EarInputVst::isReverbVstAvailable(ReaperAPI const& api, bool doRescan)
{
    return PluginRegistry::getInstance()->checkPluginAvailable(reverbVstName, api, doRescan);
}

bool EarInputVst::isReverbPlugin(const std::string& vstNameStr)
{
    // Same recognized-forks pattern as isObjectPlugin() above: "LF
    // Reverb", "LF IR Reverb", "LF Multi-Encoder" and "LF Object OSC Stereo"
    // are four separate plugins, each a multi-object input with its own
    // numPointsForMultiObjectPlugin() point count, but otherwise read
    // identically here (see numPoints()/pointParamBaseIndex()). "LF Object
    // Stereo" is a fixed 2-point case (its L/R pair) rather than a
    // variable/activatable set like the other three, but the export
    // contract (N instance-id params then N*8 position/gain fields, and
    // nothing else) is identical - see object_stereo_plugin_processor.cpp.
    static const std::vector<std::string> recognizedReverbPluginNames = {
        reverbVstName,
        "LF IR Reverb",
        "LF Multi-Encoder",
        "LF Object OSC Stereo",
    };
    return std::find(recognizedReverbPluginNames.begin(), recognizedReverbPluginNames.end(),
                     vstNameStr) != recognizedReverbPluginNames.end();
}

int EarInputVst::numPointsForMultiObjectPlugin(const std::string& vstNameStr)
{
    if (vstNameStr == reverbVstName || vstNameStr == "LF IR Reverb")
        return 10;
    if (vstNameStr == "LF Multi-Encoder")
        return 24;
    if (vstNameStr == "LF Object OSC Stereo")
        return 2;
    return 0;
}

bool EarInputVst::isInputPlugin(const std::string& vstName)
{
    return isObjectPlugin(vstName) || isDirectSpeakersPlugin(vstName) || isHoaPlugin(vstName) ||
           isReverbPlugin(vstName);
}

bool EarInputVst::isInputPlugin(ReaperAPI const& api, MediaTrack *trk, int vstPos) {
    std::string name;
    if (!api.TrackFX_GetActualFXName(trk, vstPos, name)) {
        return false;
    }

    api.CleanFXName(name);
    return isInputPlugin(name);
}

EarInputVst::EarInputVst(MediaTrack * mediaTrack, int fxIndex, ReaperAPI const & api) : PluginInstance(mediaTrack, api) {

    std::string trackVstName;
    if (!api.TrackFX_GetActualFXName(mediaTrack, fxIndex, trackVstName)) {
        throw std::runtime_error("Cannot get VST Name");
    }
    api.CleanFXName(trackVstName);
    if (!isInputPlugin(trackVstName)) {
        throw std::runtime_error("VST Name is not a known EAR Input VST");
    }

    name = trackVstName;
    guid = std::make_unique<ReaperGUID>(api.TrackFX_GetFXGUID(mediaTrack, fxIndex));

    if (isReverbPlugin(name)) {
        for (int i = 0; i < numPoints(); ++i) {
            paramReverbInstanceIds[static_cast<size_t>(i)] = createPluginParameter(i, { 0x0, 0xFFFF });
        }
    }
}

int EarInputVst::getTrackMapping()
{
    assert(paramTrackMapping);
    auto optVal = getParameterWithConvertToInt(*paramTrackMapping);
    assert(optVal.has_value());
    return *optVal;
}

int EarInputVst::getInputInstanceId() {
    if(isObjectPlugin(name)) {
        assert(paramObjectInstanceId);
        auto optVal = getParameterWithConvertToInt(*paramObjectInstanceId);
        assert(optVal.has_value());
        return *optVal;
    }
    if(isDirectSpeakersPlugin(name)) {
        assert(paramDirectSpeakersInstanceId);
        auto optVal = getParameterWithConvertToInt(*paramDirectSpeakersInstanceId);
        assert(optVal.has_value());
        return *optVal;
    }
    if(isHoaPlugin(name)) {
        assert(paramHoaInstanceId);
        auto optVal = getParameterWithConvertToInt(*paramHoaInstanceId);
        assert(optVal.has_value());
        return *optVal;
    }
    throw std::runtime_error("No instance ID parameter for this plugin type");
}

bool EarInputVst::matchesInputInstanceId(uint32_t searchId) {
    if (isReverbPlugin(name)) {
        for (int i = 0; i < numPoints(); ++i) {
            auto optVal = getParameterWithConvertToInt(*paramReverbInstanceIds[static_cast<size_t>(i)]);
            if (optVal.has_value() && static_cast<uint32_t>(*optVal) == searchId) {
                matchedPointIndex_ = i;
                return true;
            }
        }
        return false;
    }
    return static_cast<uint32_t>(getInputInstanceId()) == searchId;
}

// EarSceneMasterVst

std::string EarSceneMasterVst::vstName = admplug::EARPluginSuite::SCENEMASTER_PLUGIN_NAME;

const std::string* EarSceneMasterVst::getVstNameStr()
{
    return &vstName;
}

bool EarSceneMasterVst::isAvailable(ReaperAPI const& api, bool doRescan)
{
    return PluginRegistry::getInstance()->checkPluginAvailable(vstName, api, doRescan);
}

bool EarSceneMasterVst::isCandidateForExport(std::shared_ptr<EarSceneMasterVst> possibleCandidate)
{
    assert(possibleCandidate);
    bool isCandidate = true;
    isCandidate &= !possibleCandidate->isBypassed();
    isCandidate &= !possibleCandidate->isPluginOffline();
    isCandidate &= (possibleCandidate->getSampleRate() > 0);
    isCandidate &= (possibleCandidate->getCommandSocketPort() > 0);
    isCandidate &= (possibleCandidate->getSamplesSocketPort() > 0);
    return isCandidate;
}

std::vector<int> EarSceneMasterVst::trackEarSceneMasterVstIndexes(ReaperAPI const& api, MediaTrack *trk)
{
    std::vector<int> vstPos;

    auto trackVstNames = api.TrackFX_GetActualFXNames(trk);
    for (int i = 0; i < trackVstNames.size(); ++i) {
        api.CleanFXName(trackVstNames[i]);
        if (trackVstNames[i] == vstName) {
            vstPos.push_back(i);
        }
    }

    return vstPos;
}

bool EarSceneMasterVst::isCommunicatorPresent()
{
    return (bool)communicator;
}

bool EarSceneMasterVst::obtainCommunicator()
{
    if(isBypassed() || isPluginOffline()) return false; // Do not create if Offline - it isn't running and therefore won't connect!
    auto samplesPort = getSamplesSocketPort();
    auto commandPort = getCommandSocketPort();
    if(commandPort == 0) return false; // If the vst failed to load, it can still appear in the proj as online and not bypassed - commandPort will be zero though.
    if(isCommunicatorPresent()) releaseCommunicator();
    communicator = CommunicatorRegistry::getCommunicator<EarVstCommunicator>(samplesPort, commandPort);
    return true;
}

void EarSceneMasterVst::releaseCommunicator()
{
    communicator.reset();
}

EarSceneMasterVst::EarSceneMasterVst(MediaTrack * mediaTrack, ReaperAPI const & api) : PluginInstance(mediaTrack, api)
{
    auto index = api.TrackFX_AddByActualName(mediaTrack, EARPluginSuite::SCENEMASTER_PLUGIN_NAME, false, TrackFXAddMode::CreateIfMissing);
    if(index < 0) {
        throw std::runtime_error("Could not add to or get plugin from track");
    }
    name = EARPluginSuite::SCENEMASTER_PLUGIN_NAME;
    guid = std::make_unique<ReaperGUID>(api.TrackFX_GetFXGUID(mediaTrack, index));
}

EarSceneMasterVst::EarSceneMasterVst(MediaTrack * mediaTrack, int fxIndex, ReaperAPI const & api) : PluginInstance(mediaTrack, api)
{
    std::string trackVstName;
    bool getNameSuccess = api.TrackFX_GetActualFXName(mediaTrack, fxIndex, trackVstName);
    if (getNameSuccess) {
        api.CleanFXName(trackVstName);
    }
    if (!getNameSuccess || trackVstName != EARPluginSuite::SCENEMASTER_PLUGIN_NAME) {
        throw std::runtime_error("Plugin is not an EAR Scene plugin");
    }

    name = EARPluginSuite::SCENEMASTER_PLUGIN_NAME;
    guid = std::make_unique<ReaperGUID>(api.TrackFX_GetFXGUID(mediaTrack, fxIndex));
}

EarSceneMasterVst::~EarSceneMasterVst()
{
}

int EarSceneMasterVst::getSampleRate()
{
    if(!isCommunicatorPresent() && !obtainCommunicator()) return 0;
    return communicator->getReportedSampleRate();
}

int EarSceneMasterVst::getChannelCount()
{
    if(!isCommunicatorPresent() && !obtainCommunicator()) return 0;
    return communicator->getReportedChannelCount();
}

std::string EarSceneMasterVst::getAdmTemplateStr()
{
    if(!isCommunicatorPresent() && !obtainCommunicator()) return "";
    return communicator->getAdmTemplateStr();
}

std::vector<EarVstCommunicator::ChannelMapping> EarSceneMasterVst::getChannelMappings()
{
    if(!isCommunicatorPresent() && !obtainCommunicator()) return std::vector<EarVstCommunicator::ChannelMapping>();
    return communicator->getChannelMappings();
}

bool EarSceneMasterVst::getRenderInProgressState()
{
    if(!isCommunicatorPresent()) return false;
    return communicator->getRenderingState();
}

void EarSceneMasterVst::setRenderInProgressState(bool state)
{
    if(state == true && !isCommunicatorPresent()) obtainCommunicator();
    if(isCommunicatorPresent()) communicator->setRenderingState(state);
}

EarVstCommunicator * EarSceneMasterVst::getCommunicator(bool mustExist)
{
    if(mustExist && !isCommunicatorPresent()) obtainCommunicator();
    return communicator.get();
}

int EarSceneMasterVst::getSamplesSocketPort()
{
    auto optVal = getParameterWithConvertToInt(*paramSamplesPort);
    assert(optVal.has_value());
    return *optVal;
}

int EarSceneMasterVst::getCommandSocketPort()
{
    auto optVal = getParameterWithConvertToInt(*paramCommandPort);
    assert(optVal.has_value());
    return *optVal;
}



EarVstCommunicator::EarVstCommunicator(int samplesPort, int commandPort) : CommunicatorBase(samplesPort, commandPort)
{
}

void EarVstCommunicator::updateInfo()
{
    infoExchange();
    admAndMappingExchange();
}

bool EarVstCommunicator::copyNextFrame(float * buf, bool bypassAvailabilityCheck)
{
    if(!bypassAvailabilityCheck && !nextFrameAvailable()) return false;

    // Need to pick and select data out of buffer to build frame.
    bool successful = true;
    int currentReadPosChannelNumber = 0;

    for(auto &channelMapping : channelMappings) {
        int advanceAmount = channelMapping.originalChannelNumber - currentReadPosChannelNumber;

        // We know that channelMappings is sorted according by originalChannel, but sanity check
        assert(advanceAmount >= 0);
        if(advanceAmount > 0) {
            latestBlockMessage->advanceSeqReadPos(advanceAmount);
            currentReadPosChannelNumber += advanceAmount;
        }

        successful = latestBlockMessage->seqReadAndPut(buf + channelMapping.writtenChannelNumber, 1);
        // False = Error copying frame - frame might not have been ready.
        // This should never occur.
        // If you bypassAvailabilityCheck, you should have called nextFrameAvailable yourself prior to this call.
        assert(successful);
        if(!successful) break;

        currentReadPosChannelNumber++;
    }

    // Advance to the end of this data
    int advanceAmount = MAX_DAW_CHANNELS - currentReadPosChannelNumber;
    // Sanity check; Make sure we haven't already exceeded the end of the message
    assert(advanceAmount >= 0);
    if(advanceAmount > 0) {
        latestBlockMessage->advanceSeqReadPos(advanceAmount);
    }

    return successful;
}

void EarVstCommunicator::sendAdm(std::string originalAdmStr, std::vector<PluginToAdmMap> pluginToAdmMaps)
{
    if(commandSocket.isSocketOpen()) {
        auto resp = commandSocket.sendAdmAndMappings(originalAdmStr, pluginToAdmMaps);
    }
}

void EarVstCommunicator::infoExchange()
{
    auto resp = commandSocket.doCommand(commandSocket.Command::GetConfig);
    assert(resp->success());

    memcpy(&channelCount, (char*)resp->getBufferPointer() + 0, 1);
    memcpy(&sampleRate, (char*)resp->getBufferPointer() + 1, 4);
}

void EarVstCommunicator::admAndMappingExchange()
{
    auto resp = commandSocket.doCommand(commandSocket.Command::GetAdmAndMappings);
    assert(resp->success());

    std::string admStrRecv;
    std::vector<PluginToAdmMap> pluginToAdmMaps;
    commandSocket.decodeAdmAndMappingsMessage(resp, admStrRecv, pluginToAdmMaps);

    // channelMappings must be sorted by originalChannel (which, in turn should be sorted for writtenChannelNumber too)
    // This makes it faster for the copyNextFrame method to jump between channels

    std::vector<std::vector<PluginToAdmMap>>routingToPluginToAdmMaps(MAX_DAW_CHANNELS);

    for(auto const& pluginToAdmMap : pluginToAdmMaps) {
        if(pluginToAdmMap.audioObjectIdVal != 0x0000 &&
           pluginToAdmMap.audioTrackUidVal != 0x00000000 &&
           pluginToAdmMap.routing >= 0 &&
           pluginToAdmMap.routing < MAX_DAW_CHANNELS) {
            routingToPluginToAdmMaps[pluginToAdmMap.routing].push_back(pluginToAdmMap);
        }
    }

    auto latestChannelMappings = std::vector<ChannelMapping>();

    uint8_t writtenChannelNum = 0;
    for(uint8_t routingVal = 0; routingVal < routingToPluginToAdmMaps.size(); ++routingVal) {
        if(!routingToPluginToAdmMaps[routingVal].empty()) {
            auto channelMapping = ChannelMapping{ routingVal, writtenChannelNum, {} };
            writtenChannelNum++;
            for(auto const& pluginToAdmMap : routingToPluginToAdmMaps[routingVal]) {
                channelMapping.plugins.push_back(pluginToAdmMap);
            }
            latestChannelMappings.push_back(channelMapping);
        }
    }

    channelMappings = latestChannelMappings;
    admStr = admStrRecv;
}

//HOA functions

std::string EarInputVst::hoaVstName = admplug::EARPluginSuite::HOA_METADATA_PLUGIN_NAME;

const std::string* EarInputVst::getHoaVstNameStr()
{
    return &hoaVstName;
}

bool EarInputVst::isHoaVstAvailable(ReaperAPI const& api, bool doRescan)
{
    return PluginRegistry::getInstance()->checkPluginAvailable(hoaVstName, api, doRescan);
}

bool EarInputVst::isHoaPlugin(const std::string& vstNameStr)
{
    return vstNameStr == hoaVstName;
}