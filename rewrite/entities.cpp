#include <cmath>

#include "common.hpp"
#include "bsp.hpp"
#include "ConfigXML.hpp"

void parse_entities(const std::string &szStr, const std::string &id, const MapEntry &sMapEntry) {
    std::stringstream ss(szStr);

    int status = 0;

    std::string origin;
    std::string targetname;
    std::string landmark;
    std::string modelname;
    bool isLandMark = false;
    bool isChangeLevel = false;
    bool isTeleport = false;

    std::map<std::string, int> changelevels;
    std::map<std::string, VERTEX> ret;

    while (ss.good()) {
        std::string str;
        getline(ss, str);
        if (status == 0) {
            if (str == "{") {
                status = 1, isLandMark = false, isChangeLevel = false, isTeleport = false;
            } else {
                if (ss.good()) {
                    std::cerr << "Missing stuff in entity: " << str << std::endl;
                }
            }
        } else if (status == 1) {
            if (str == "}") {
                status = 0;
                if (isLandMark) {
                    float x = NAN;
                    float y = NAN;
                    float z = NAN;
                    sscanf(origin.c_str(), "%f %f %f", &x, &y, &z);
                    VERTEX v(x, y, z);
                    v.fixHand();

                    if (sMapEntry.m_szOffsetTargetName == targetname) {
                        // Apply std::map offsets from the config, to fix landmark positions.
                        v.x += sMapEntry.m_fOffsetX;
                        v.y += sMapEntry.m_fOffsetY;
                        v.z += sMapEntry.m_fOffsetZ;
                    }

                    ret[targetname] = v;

                } else if (isChangeLevel) {
                    if (!landmark.empty()) {
                        changelevels[landmark] = 1;
                    }
                }
                if (isTeleport || isChangeLevel) {
                    dontRenderModel[id].push_back(modelname);
                }
            } else {
                if (str == R"("classname" "info_landmark")") {
                    isLandMark = true;
                }
                if (str == R"("classname" "trigger_changelevel")") {
                    isChangeLevel = true;
                }
                if (str == R"("classname" "trigger_teleport")" || str == R"("classname" "func_pendulum")" || str == R"("classname" "trigger_transition")" || str == R"("classname" "trigger_hurt")" || str == R"("classname" "func_train")" || str == R"("classname" "func_door_rotating")") {
                    isTeleport = true;
                }
                if (str.substr(0, 8) == "\"origin\"") {
                    origin = str.substr(10);
                    origin.erase(origin.size() - 1);
                }
                if (str.substr(0, 7) == "\"model\"") {
                    modelname = str.substr(9);
                    modelname.erase(modelname.size() - 1);
                }
                if (str.substr(0, 12) == "\"targetname\"") {
                    targetname = str.substr(14);
                    targetname.erase(targetname.size() - 1);
                }
                if (str.substr(0, 10) == "\"landmark\"") {
                    landmark = str.substr(12);
                    landmark.erase(landmark.size() - 1);
                }
            }
        }
    }
    for (auto it = ret.begin(); it != ret.end(); it++) {
        if (changelevels.contains((*it).first)) {
            landmarks[(*it).first].push_back(make_pair((*it).second, id));
        }
    }
}
