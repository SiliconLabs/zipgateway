#ifndef SDK_VERSIONING_H
#define SDK_VERSIONING_H

/**
 * Convert the SDK version to the NVM ID.
 *
 *    @param sdk_ver returned by GenerateSdkVersionFromProtocolVersion()
 *    @param library_type returned by ZW_Version()
 *
 *    @return nvm_id for e.g. bridge6.81
 */
const char * GenerateNvmIdFromSdkVersion(uint8_t, uint8_t, uint8_t, uint8_t library_type, uint8_t chip_type);
#endif
