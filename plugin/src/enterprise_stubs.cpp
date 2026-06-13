// enterprise_stubs.cpp
//
// Weak stub implementations for Enterprise / Collaboration API functions.
// These satisfy the static linker when building against standard (non-Enterprise)
// Binary Ninja. At runtime the real symbols from libbinaryninjacore override
// these weak definitions when running Enterprise BN.
//
// Auto-generated from binaryninjacore.h + collaboration.cpp + enterprise.cpp

#include "binaryninjacore.h"

// Suppress unused-parameter warnings inside stub bodies
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// All BN* functions have C linkage (declared extern "C" in binaryninjacore.h).
// Definitions must match — wrap in extern "C" so the linker sees the same mangled
// names as the declarations.
extern "C" {

__attribute__((weak)) char* BNAnalysisMergeConflictGetBase(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNFileMetadata* BNAnalysisMergeConflictGetBaseFile(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNSnapshot* BNAnalysisMergeConflictGetBaseSnapshot(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNMergeConflictDataType BNAnalysisMergeConflictGetDataType(BNAnalysisMergeConflict* conflict) { return (BNMergeConflictDataType)0; }
__attribute__((weak)) char* BNAnalysisMergeConflictGetFirst(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNFileMetadata* BNAnalysisMergeConflictGetFirstFile(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNSnapshot* BNAnalysisMergeConflictGetFirstSnapshot(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) void* BNAnalysisMergeConflictGetPathItem(BNAnalysisMergeConflict* conflict, const char* path) { return NULL; }
__attribute__((weak)) char* BNAnalysisMergeConflictGetPathItemSerialized(BNAnalysisMergeConflict* conflict, const char* path) { return NULL; }
__attribute__((weak)) char* BNAnalysisMergeConflictGetPathItemString(BNAnalysisMergeConflict* conflict, const char* path) { return NULL; }
__attribute__((weak)) char* BNAnalysisMergeConflictGetSecond(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNFileMetadata* BNAnalysisMergeConflictGetSecondFile(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNSnapshot* BNAnalysisMergeConflictGetSecondSnapshot(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) char* BNAnalysisMergeConflictGetType(BNAnalysisMergeConflict* conflict) { return NULL; }
__attribute__((weak)) bool BNAnalysisMergeConflictSuccess(BNAnalysisMergeConflict* conflict, const char* value) { return false; }
__attribute__((weak)) bool BNAuthenticateEnterpriseServerWithCredentials(const char* username, const char* password, bool remember) { return false; }
__attribute__((weak)) bool BNAuthenticateEnterpriseServerWithMethod(const char* method, bool remember) { return false; }
__attribute__((weak)) void BNCancelEnterpriseServerAuthentication(void) { (void)0; }
__attribute__((weak)) bool BNCollaborationAssignSnapshotMap(BNSnapshot* localSnapshot, BNCollaborationSnapshot* remoteSnapshot) { return false; }
__attribute__((weak)) BNCollaborationUser* BNCollaborationChangesetGetAuthor(BNCollaborationChangeset* changeset) { return NULL; }
__attribute__((weak)) BNDatabase* BNCollaborationChangesetGetDatabase(BNCollaborationChangeset* changeset) { return NULL; }
__attribute__((weak)) BNRemoteFile* BNCollaborationChangesetGetFile(BNCollaborationChangeset* changeset) { return NULL; }
__attribute__((weak)) char* BNCollaborationChangesetGetName(BNCollaborationChangeset* changeset) { return NULL; }
__attribute__((weak)) int64_t* BNCollaborationChangesetGetSnapshotIds(BNCollaborationChangeset* changeset, size_t* count) { return NULL; }
__attribute__((weak)) bool BNCollaborationChangesetSetName(BNCollaborationChangeset* changeset, const char* name) { return false; }
__attribute__((weak)) BNRemote* BNCollaborationCreateRemote(const char* name, const char* address) { return NULL; }
__attribute__((weak)) char* BNCollaborationDefaultFilePath(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNCollaborationDefaultProjectPath(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) bool BNCollaborationDeleteDataFromKeychain(const char* key) { return false; }
__attribute__((weak)) bool BNCollaborationDownloadDatabaseForFile(BNRemoteFile* file, const char* dbPath, bool force, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) BNFileMetadata* BNCollaborationDownloadFile(BNRemoteFile* file, const char* dbPath, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) bool BNCollaborationDownloadTypeArchive(BNRemoteFile* file, const char* dbPath, BNProgressFunction progress, void* progressContext, BNTypeArchive** result) { return false; }
__attribute__((weak)) bool BNCollaborationDumpDatabase(BNDatabase* database) { return false; }
__attribute__((weak)) BNRemote* BNCollaborationGetActiveRemote(void) { return NULL; }
__attribute__((weak)) size_t BNCollaborationGetDataFromKeychain(const char* key, char*** foundKeys, char*** foundValues) { return 0; }
__attribute__((weak)) bool BNCollaborationGetLocalSnapshotFromRemote(BNCollaborationSnapshot* snapshot, BNDatabase* database, BNSnapshot** result) { return false; }
__attribute__((weak)) char* BNCollaborationGetLocalSnapshotFromRemoteTypeArchive(BNCollaborationSnapshot* snapshot, BNTypeArchive* archive) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationGetRemoteByAddress(const char* remoteAddress) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationGetRemoteById(const char* remoteId) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationGetRemoteByName(const char* name) { return NULL; }
__attribute__((weak)) bool BNCollaborationGetRemoteFileForLocalDatabase(BNDatabase* database, BNRemoteFile** result) { return false; }
__attribute__((weak)) BNRemoteFile* BNCollaborationGetRemoteFileForLocalTypeArchive(BNTypeArchive* archive) { return NULL; }
__attribute__((weak)) bool BNCollaborationGetRemoteForLocalDatabase(BNDatabase* database, BNRemote** result) { return false; }
__attribute__((weak)) BNRemote* BNCollaborationGetRemoteForLocalTypeArchive(BNTypeArchive* archive) { return NULL; }
__attribute__((weak)) bool BNCollaborationGetRemoteProjectForLocalDatabase(BNDatabase* database, BNRemoteProject** result) { return false; }
__attribute__((weak)) BNRemoteProject* BNCollaborationGetRemoteProjectForLocalTypeArchive(BNTypeArchive* archive) { return NULL; }
__attribute__((weak)) bool BNCollaborationGetRemoteSnapshotFromLocal(BNSnapshot* snapshot, BNCollaborationSnapshot** result) { return false; }
__attribute__((weak)) BNCollaborationSnapshot* BNCollaborationGetRemoteSnapshotFromLocalTypeArchive(BNTypeArchive* archive, const char* snapshotId) { return NULL; }
__attribute__((weak)) BNRemote** BNCollaborationGetRemotes(size_t* count) { return NULL; }
__attribute__((weak)) bool BNCollaborationGetSnapshotAuthor(BNDatabase* database, BNSnapshot* snapshot, char** result) { return false; }
__attribute__((weak)) bool BNCollaborationGroupContainsUser(BNCollaborationGroup* group, BNCollaborationUser* user) { return false; }
__attribute__((weak)) uint64_t BNCollaborationGroupGetId(BNCollaborationGroup* group) { return 0; }
__attribute__((weak)) char* BNCollaborationGroupGetName(BNCollaborationGroup* group) { return NULL; }
__attribute__((weak)) void BNCollaborationGroupSetName(BNCollaborationGroup* group, const char* name) { (void)0; }
__attribute__((weak)) BNCollaborationUser** BNCollaborationGroupGetUsers(BNCollaborationGroup* group, size_t* count) { if (count) *count = 0; return NULL; }
__attribute__((weak)) bool BNCollaborationGroupSetUsers(BNCollaborationGroup* group, BNCollaborationUser** users, size_t count) { return false; }
__attribute__((weak)) bool BNCollaborationGroupSetUsernames(BNCollaborationGroup* group, const char** names, size_t count) { return false; }
__attribute__((weak)) bool BNCollaborationHasDataInKeychain(const char* key) { return false; }
__attribute__((weak)) bool BNCollaborationIgnoreSnapshot(BNDatabase* database, BNSnapshot* snapshot) { return false; }
__attribute__((weak)) bool BNCollaborationIsCollaborationDatabase(BNDatabase* database) { return false; }
__attribute__((weak)) bool BNCollaborationIsCollaborationTypeArchive(BNTypeArchive* archive) { return false; }
__attribute__((weak)) bool BNCollaborationIsSnapshotIgnored(BNDatabase* database, BNSnapshot* snapshot) { return false; }
__attribute__((weak)) bool BNCollaborationIsTypeArchiveSnapshotIgnored(BNTypeArchive* archive, const char* snapshot) { return false; }
__attribute__((weak)) bool BNCollaborationLoadRemotes(void) { return false; }
__attribute__((weak)) bool BNCollaborationMergeDatabase(BNDatabase* database, BNCollaborationAnalysisConflictHandler conflictHandler, void* conflictHandlerCtxt, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) BNSnapshot* BNCollaborationMergeSnapshots(BNSnapshot* first, BNSnapshot* second, BNCollaborationAnalysisConflictHandler conflictHandler, void* conflictHandlerCtxt, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) bool BNCollaborationPermissionCanAdmin(BNCollaborationPermission* permission) { return false; }
__attribute__((weak)) bool BNCollaborationPermissionCanEdit(BNCollaborationPermission* permission) { return false; }
__attribute__((weak)) bool BNCollaborationPermissionCanView(BNCollaborationPermission* permission) { return false; }
__attribute__((weak)) uint64_t BNCollaborationPermissionGetGroupId(BNCollaborationPermission* permission) { return 0; }
__attribute__((weak)) char* BNCollaborationPermissionGetGroupName(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) char* BNCollaborationPermissionGetId(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) BNCollaborationPermissionLevel BNCollaborationPermissionGetLevel(BNCollaborationPermission* permission) { return (BNCollaborationPermissionLevel)0; }
__attribute__((weak)) BNRemoteProject* BNCollaborationPermissionGetProject(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationPermissionGetRemote(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) char* BNCollaborationPermissionGetUrl(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) char* BNCollaborationPermissionGetUserId(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) char* BNCollaborationPermissionGetUsername(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) void BNCollaborationPermissionSetLevel(BNCollaborationPermission* permission, BNCollaborationPermissionLevel level) { (void)0; }
__attribute__((weak)) bool BNCollaborationPullDatabase(BNDatabase* database, BNRemoteFile* file, size_t* count, BNCollaborationAnalysisConflictHandler conflictHandler, void* conflictHandlerCtxt, BNProgressFunction progress, void* progressContext, BNCollaborationNameChangesetFunction nameChangeset, void* nameChangesetContext) { return false; }
__attribute__((weak)) bool BNCollaborationPullTypeArchive(BNTypeArchive* archive, BNRemoteFile* file, size_t* count, bool(*conflictHandler)(void*, BNTypeArchiveMergeConflict** conflicts, size_t conflictCount), void* conflictHandlerCtxt, BNProgressFunction progress, void* progressCtxt) { return false; }
__attribute__((weak)) bool BNCollaborationPushDatabase(BNDatabase* database, BNRemoteFile* file, size_t* count, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNCollaborationPushTypeArchive(BNTypeArchive* archive, BNRemoteFile* file, size_t* count, BNProgressFunction progress, void* progressCtxt) { return false; }
__attribute__((weak)) void BNCollaborationRemoveRemote(BNRemote* remote) { (void)0; }
__attribute__((weak)) void BNCollaborationSetActiveRemote(BNRemote* remote) { (void)0; }
__attribute__((weak)) bool BNCollaborationSetSnapshotAuthor(BNDatabase* database, BNSnapshot* snapshot, const char* author) { return false; }
__attribute__((weak)) BNCollaborationUndoEntry* BNCollaborationSnapshotCreateUndoEntry(BNCollaborationSnapshot* snapshot, bool hasParent, uint64_t parent, const char* data) { return NULL; }
__attribute__((weak)) bool BNCollaborationSnapshotDownload(BNCollaborationSnapshot* snapshot, BNProgressFunction progress, void* progressContext, uint8_t** data, size_t* size) { return false; }
__attribute__((weak)) bool BNCollaborationSnapshotDownloadAnalysisCache(BNCollaborationSnapshot* snapshot, BNProgressFunction progress, void* progressContext, uint8_t** data, size_t* size) { return false; }
__attribute__((weak)) bool BNCollaborationSnapshotDownloadSnapshotFile(BNCollaborationSnapshot* snapshot, BNProgressFunction progress, void* progressContext, uint8_t** data, size_t* size) { return false; }
__attribute__((weak)) bool BNCollaborationSnapshotFinalize(BNCollaborationSnapshot* snapshot) { return false; }
__attribute__((weak)) uint64_t BNCollaborationSnapshotGetAnalysisCacheBuildId(BNCollaborationSnapshot* snapshot) { return 0; }
__attribute__((weak)) char* BNCollaborationSnapshotGetAuthor(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetAuthorUsername(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) BNCollaborationSnapshot** BNCollaborationSnapshotGetChildren(BNCollaborationSnapshot* snapshot, size_t* count) { return NULL; }
__attribute__((weak)) int64_t BNCollaborationSnapshotGetCreated(BNCollaborationSnapshot* snapshot) { return 0; }
__attribute__((weak)) char* BNCollaborationSnapshotGetDescription(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) BNRemoteFile* BNCollaborationSnapshotGetFile(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetHash(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetId(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) int64_t BNCollaborationSnapshotGetLastModified(BNCollaborationSnapshot* snapshot) { return 0; }
__attribute__((weak)) char* BNCollaborationSnapshotGetName(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char** BNCollaborationSnapshotGetParentIds(BNCollaborationSnapshot* snapshot, size_t* count) { return NULL; }
__attribute__((weak)) BNCollaborationSnapshot** BNCollaborationSnapshotGetParents(BNCollaborationSnapshot* snapshot, size_t* count) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNCollaborationSnapshotGetProject(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationSnapshotGetRemote(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetSnapshotFileHash(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetTitle(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) BNCollaborationUndoEntry** BNCollaborationSnapshotGetUndoEntries(BNCollaborationSnapshot* snapshot, size_t* count) { return NULL; }
__attribute__((weak)) BNCollaborationUndoEntry* BNCollaborationSnapshotGetUndoEntryById(BNCollaborationSnapshot* snapshot, uint64_t id) { return NULL; }
__attribute__((weak)) char* BNCollaborationSnapshotGetUrl(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) bool BNCollaborationSnapshotHasPulledUndoEntries(BNCollaborationSnapshot* snapshot) { return false; }
__attribute__((weak)) bool BNCollaborationSnapshotIsFinalized(BNCollaborationSnapshot* snapshot) { return false; }
__attribute__((weak)) bool BNCollaborationSnapshotPullUndoEntries(BNCollaborationSnapshot* snapshot, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNCollaborationStoreDataInKeychain(const char* key, const char** dataKeys, const char** dataValues, size_t dataCount) { return false; }
__attribute__((weak)) bool BNCollaborationSyncDatabase(BNDatabase* database, BNRemoteFile* file, BNCollaborationAnalysisConflictHandler conflictHandler, void* conflictHandlerCtxt, BNProgressFunction progress, void* progressCtxt, BNCollaborationNameChangesetFunction nameChangeset, void* nameChangesetCtxt) { return false; }
__attribute__((weak)) bool BNCollaborationSyncTypeArchive(BNTypeArchive* archive, BNRemoteFile* file, bool(*conflictHandler)(void*, BNTypeArchiveMergeConflict** conflicts, size_t conflictCount), void* conflictHandlerCtxt, BNProgressFunction progress, void* progressCtxt) { return false; }
__attribute__((weak)) BNRemoteFile* BNCollaborationUploadDatabase(BNFileMetadata* metadata, BNRemoteProject* project, BNRemoteFolder* folder, BNProgressFunction progress, void* progressContext, BNCollaborationNameChangesetFunction nameChangeset, void* nameChangesetContext) { return NULL; }
__attribute__((weak)) bool BNCollaborationUploadTypeArchive(BNTypeArchive* archive, BNRemoteProject* project, BNRemoteFolder* folder, BNProgressFunction progress, void* progressContext, BNProjectFile* coreFile, BNRemoteFile** result) { return false; }
__attribute__((weak)) char* BNCollaborationUserGetEmail(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) char* BNCollaborationUserGetId(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) char* BNCollaborationUserGetLastLogin(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) BNRemote* BNCollaborationUserGetRemote(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) char* BNCollaborationUserGetUrl(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) char* BNCollaborationUserGetUsername(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) bool BNCollaborationUserIsActive(BNCollaborationUser* user) { return false; }
__attribute__((weak)) bool BNCollaborationUserSetEmail(BNCollaborationUser* user, const char* email) { return false; }
__attribute__((weak)) bool BNCollaborationUserSetIsActive(BNCollaborationUser* user, bool isActive) { return false; }
__attribute__((weak)) bool BNCollaborationUserSetUsername(BNCollaborationUser* user, const char* username) { return false; }
__attribute__((weak)) bool BNConnectEnterpriseServer(void) { return false; }
__attribute__((weak)) bool BNDeauthenticateEnterpriseServer(void) { return false; }
__attribute__((weak)) void BNFreeCollaborationGroupList(BNCollaborationGroup** group, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeCollaborationPermissionList(BNCollaborationPermission** permissions, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeCollaborationSnapshotList(BNCollaborationSnapshot** snapshots, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeCollaborationUndoEntryList(BNCollaborationUndoEntry** entries, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeCollaborationUserList(BNCollaborationUser** users, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeRemoteFileList(BNRemoteFile** files, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeRemoteFileSearchMatchList(BNRemoteFileSearchMatch* matches, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeRemoteFolderList(BNRemoteFolder** folders, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeRemoteList(BNRemote** remotes, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeRemoteProjectList(BNRemoteProject** projects, size_t count) { (void)0; }
__attribute__((weak)) void BNFreeString(char* str) { (void)0; }
__attribute__((weak)) void BNFreeStringList(char** strs, size_t count) { (void)0; }
__attribute__((weak)) size_t BNGetEnterpriseServerAuthenticationMethods(char*** methods, char*** names) { return 0; }
__attribute__((weak)) char* BNGetEnterpriseServerBuildId(void) { return NULL; }
__attribute__((weak)) char* BNGetEnterpriseServerId(void) { return NULL; }
__attribute__((weak)) char* BNGetEnterpriseServerLastError(void) { return NULL; }
__attribute__((weak)) uint64_t BNGetEnterpriseServerLicenseDuration(void) { return 0; }
__attribute__((weak)) uint64_t BNGetEnterpriseServerLicenseExpirationTime(void) { return 0; }
__attribute__((weak)) char* BNGetEnterpriseServerName(void) { return NULL; }
__attribute__((weak)) uint64_t BNGetEnterpriseServerReservationTimeLimit(void) { return 0; }
__attribute__((weak)) char* BNGetEnterpriseServerToken(void) { return NULL; }
__attribute__((weak)) char* BNGetEnterpriseServerUrl(void) { return NULL; }
__attribute__((weak)) char* BNGetEnterpriseServerUsername(void) { return NULL; }
__attribute__((weak)) uint64_t BNGetEnterpriseServerVersion(void) { return 0; }
__attribute__((weak)) uint64_t BNGetLicenseExpirationTime(void) { return 0; }
__attribute__((weak)) bool BNInitializeEnterpriseServer(void) { return false; }
__attribute__((weak)) bool BNIsEnterpriseServerAuthenticated(void) { return false; }
__attribute__((weak)) bool BNIsEnterpriseServerConnected(void) { return false; }
__attribute__((weak)) bool BNIsEnterpriseServerFloatingLicense(void) { return false; }
__attribute__((weak)) bool BNIsEnterpriseServerInitialized(void) { return false; }
__attribute__((weak)) bool BNIsEnterpriseServerLicenseStillActivated(void) { return false; }
__attribute__((weak)) bool BNIsUIEnabled(void) { return false; }
__attribute__((weak)) BNCollaborationGroup* BNNewCollaborationGroupReference(BNCollaborationGroup* group) { return NULL; }
__attribute__((weak)) BNCollaborationPermission* BNNewCollaborationPermissionReference(BNCollaborationPermission* permission) { return NULL; }
__attribute__((weak)) BNCollaborationSnapshot* BNNewCollaborationSnapshotReference(BNCollaborationSnapshot* snapshot) { return NULL; }
__attribute__((weak)) BNCollaborationUndoEntry* BNNewCollaborationUndoEntryReference(BNCollaborationUndoEntry* entry) { return NULL; }
__attribute__((weak)) BNCollaborationUser* BNNewCollaborationUserReference(BNCollaborationUser* user) { return NULL; }
__attribute__((weak)) BNRemoteFile* BNNewRemoteFileReference(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) BNRemoteFolder* BNNewRemoteFolderReference(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNNewRemoteProjectReference(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) BNRemote* BNNewRemoteReference(BNRemote* remote) { return NULL; }
__attribute__((weak)) void BNRegisterEnterpriseServerNotification(BNEnterpriseServerCallbacks* notify) { (void)0; }
__attribute__((weak)) bool BNReleaseEnterpriseServerLicense(void) { return false; }
__attribute__((weak)) bool BNRemoteConnect(BNRemote* remote, const char* username, const char* token) { return false; }
__attribute__((weak)) BNCollaborationGroup* BNRemoteCreateGroup(BNRemote* remote, const char* name, BNCollaborationUser** users, size_t userCount) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNRemoteCreateProject(BNRemote* remote, const char* name, const char* description) { return NULL; }
__attribute__((weak)) BNCollaborationUser* BNRemoteCreateUser(BNRemote* remote, const char* username, const char* email, bool isActive, const char* password, const uint64_t* groupIds, size_t groupIdCount, const uint64_t* userPermissionIds, size_t userPermissionIdCount) { return NULL; }
__attribute__((weak)) bool BNRemoteDeleteGroup(BNRemote* remote, BNCollaborationGroup* group) { return false; }
__attribute__((weak)) bool BNRemoteDeleteProject(BNRemote* remote, BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteDisconnect(BNRemote* remote) { return false; }
__attribute__((weak)) BNCollaborationSnapshot* BNRemoteFileCreateSnapshot(BNRemoteFile* file, const char* name, uint8_t* contents, size_t contentsSize, uint8_t* analysisCacheContents, size_t analysisCacheContentsSize, uint8_t* fileContents, size_t fileContentsSize, const char** parentIds, size_t parentIdCount, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) bool BNRemoteFileDeleteSnapshot(BNRemoteFile* file, BNCollaborationSnapshot* snapshot) { return false; }
__attribute__((weak)) bool BNRemoteFileDownload(BNRemoteFile* file, BNProgressFunction progress, void* progressCtxt) { return false; }
__attribute__((weak)) bool BNRemoteFileDownloadContents(BNRemoteFile* file, BNProgressFunction progress, void* progressCtxt, uint8_t** data, size_t* size) { return false; }
__attribute__((weak)) char* BNRemoteFileGetChatLogUrl(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) BNProjectFile* BNRemoteFileGetCoreFile(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) int64_t BNRemoteFileGetCreated(BNRemoteFile* file) { return 0; }
__attribute__((weak)) char* BNRemoteFileGetCreatedBy(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetDescription(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) BNRemoteFolder* BNRemoteFileGetFolder(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetHash(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetId(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) int64_t BNRemoteFileGetLastModified(BNRemoteFile* file) { return 0; }
__attribute__((weak)) int64_t BNRemoteFileGetLastSnapshot(BNRemoteFile* file) { return 0; }
__attribute__((weak)) char* BNRemoteFileGetLastSnapshotBy(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetLastSnapshotName(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetMetadata(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetName(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNRemoteFileGetProject(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) BNRemote* BNRemoteFileGetRemote(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) uint64_t BNRemoteFileGetSize(BNRemoteFile* file) { return 0; }
__attribute__((weak)) BNCollaborationSnapshot* BNRemoteFileGetSnapshotById(BNRemoteFile* file, const char* id) { return NULL; }
__attribute__((weak)) BNCollaborationSnapshot** BNRemoteFileGetSnapshots(BNRemoteFile* file, size_t* count) { return NULL; }
__attribute__((weak)) BNRemoteFileType BNRemoteFileGetType(BNRemoteFile* file) { return (BNRemoteFileType)0; }
__attribute__((weak)) char* BNRemoteFileGetUrl(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileGetUserPositionsUrl(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) bool BNRemoteFileHasPulledSnapshots(BNRemoteFile* file) { return false; }
__attribute__((weak)) bool BNRemoteFilePullSnapshots(BNRemoteFile* file, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) char* BNRemoteFileRequestChatLog(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) char* BNRemoteFileRequestUserPositions(BNRemoteFile* file) { return NULL; }
__attribute__((weak)) bool BNRemoteFileSetDescription(BNRemoteFile* file, const char* description) { return false; }
__attribute__((weak)) bool BNRemoteFileSetFolder(BNRemoteFile* file, BNRemoteFolder* folder) { return false; }
__attribute__((weak)) bool BNRemoteFileSetMetadata(BNRemoteFile* file, const char* metadata) { return false; }
__attribute__((weak)) bool BNRemoteFileSetName(BNRemoteFile* file, const char* name) { return false; }
__attribute__((weak)) BNRemoteFileSearchMatch* BNRemoteFindFiles(BNRemote* remote, const char* name, size_t* count) { return NULL; }
__attribute__((weak)) BNProjectFolder* BNRemoteFolderGetCoreFolder(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) char* BNRemoteFolderGetDescription(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) char* BNRemoteFolderGetId(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) char* BNRemoteFolderGetName(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) bool BNRemoteFolderGetParent(BNRemoteFolder* folder, BNRemoteFolder** parent) { return false; }
__attribute__((weak)) BNRemoteProject* BNRemoteFolderGetProject(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) BNRemote* BNRemoteFolderGetRemote(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) char* BNRemoteFolderGetUrl(BNRemoteFolder* folder) { return NULL; }
__attribute__((weak)) char* BNRemoteGetAddress(BNRemote* remote) { return NULL; }
__attribute__((weak)) bool BNRemoteGetAuthBackends(BNRemote* remote, char*** backendIds, char*** backendNames, size_t* count) { return false; }
__attribute__((weak)) BNCollaborationUser* BNRemoteGetCurrentUser(BNRemote* remote) { return NULL; }
__attribute__((weak)) BNCollaborationGroup* BNRemoteGetGroupById(BNRemote* remote, uint64_t id) { return NULL; }
__attribute__((weak)) BNCollaborationGroup* BNRemoteGetGroupByName(BNRemote* remote, const char* name) { return NULL; }
__attribute__((weak)) BNCollaborationGroup** BNRemoteGetGroups(BNRemote* remote, size_t* count) { return NULL; }
__attribute__((weak)) char* BNRemoteGetName(BNRemote* remote) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNRemoteGetProjectById(BNRemote* remote, const char* id) { return NULL; }
__attribute__((weak)) BNRemoteProject* BNRemoteGetProjectByName(BNRemote* remote, const char* name) { return NULL; }
__attribute__((weak)) BNRemoteProject** BNRemoteGetProjects(BNRemote* remote, size_t* count) { return NULL; }
__attribute__((weak)) char* BNRemoteGetServerBuildId(BNRemote* remote) { return NULL; }
__attribute__((weak)) BNVersionInfo BNRemoteGetServerBuildVersion(BNRemote* remote) { BNVersionInfo v{}; return v; }
__attribute__((weak)) int BNRemoteGetServerVersion(BNRemote* remote) { return 0; }
__attribute__((weak)) char* BNRemoteGetToken(BNRemote* remote) { return NULL; }
__attribute__((weak)) char* BNRemoteGetUniqueId(BNRemote* remote) { return NULL; }
__attribute__((weak)) BNCollaborationUser* BNRemoteGetUserById(BNRemote* remote, const char* id) { return NULL; }
__attribute__((weak)) BNCollaborationUser* BNRemoteGetUserByUsername(BNRemote* remote, const char* username) { return NULL; }
__attribute__((weak)) char* BNRemoteGetUsername(BNRemote* remote) { return NULL; }
__attribute__((weak)) BNCollaborationUser** BNRemoteGetUsers(BNRemote* remote, size_t* count) { return NULL; }
__attribute__((weak)) bool BNRemoteHasLoadedMetadata(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteHasPulledGroups(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteHasPulledProjects(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteHasPulledUsers(BNRemote* remote) { return false; }
__attribute__((weak)) BNRemoteProject* BNRemoteImportLocalProject(BNRemote* remote, BNProject* localProject, BNProgressFunction progress, void* progressCtxt) { return NULL; }
__attribute__((weak)) bool BNRemoteIsAdmin(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteIsConnected(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteIsEnterprise(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteLoadMetadata(BNRemote* remote) { return false; }
__attribute__((weak)) bool BNRemoteProjectCanUserAdmin(BNRemoteProject* project, BNCollaborationUser* user) { return false; }
__attribute__((weak)) bool BNRemoteProjectCanUserEdit(BNRemoteProject* project, BNCollaborationUser* user) { return false; }
__attribute__((weak)) bool BNRemoteProjectCanUserView(BNRemoteProject* project, BNCollaborationUser* user) { return false; }
__attribute__((weak)) void BNRemoteProjectClose(BNRemoteProject* project) { (void)0; }
__attribute__((weak)) BNRemoteFile* BNRemoteProjectCreateFile(BNRemoteProject* project, const char* filename, uint8_t* contents, size_t contentsSize, const char* name, const char* description, BNRemoteFolder* folder, BNRemoteFileType type, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) BNRemoteFolder* BNRemoteProjectCreateFolder(BNRemoteProject* project, const char* name, const char* description, BNRemoteFolder* parent, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) BNCollaborationPermission* BNRemoteProjectCreateGroupPermission(BNRemoteProject* project, int64_t groupId, BNCollaborationPermissionLevel level, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) BNCollaborationPermission* BNRemoteProjectCreateUserPermission(BNRemoteProject* project, const char* userId, BNCollaborationPermissionLevel level, BNProgressFunction progress, void* progressContext) { return NULL; }
__attribute__((weak)) bool BNRemoteProjectDeleteFile(BNRemoteProject* project, BNRemoteFile* file) { return false; }
__attribute__((weak)) bool BNRemoteProjectDeleteFolder(BNRemoteProject* project, BNRemoteFolder* folder) { return false; }
__attribute__((weak)) bool BNRemoteProjectDeletePermission(BNRemoteProject* project, BNCollaborationPermission* permission) { return false; }
__attribute__((weak)) BNProject* BNRemoteProjectGetCoreProject(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) int64_t BNRemoteProjectGetCreated(BNRemoteProject* project) { return 0; }
__attribute__((weak)) char* BNRemoteProjectGetDescription(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) BNRemoteFile* BNRemoteProjectGetFileById(BNRemoteProject* project, const char* id) { return NULL; }
__attribute__((weak)) BNRemoteFile* BNRemoteProjectGetFileByName(BNRemoteProject* project, const char* name) { return NULL; }
__attribute__((weak)) BNRemoteFile** BNRemoteProjectGetFiles(BNRemoteProject* project, size_t* count) { return NULL; }
__attribute__((weak)) BNRemoteFolder* BNRemoteProjectGetFolderById(BNRemoteProject* project, const char* id) { return NULL; }
__attribute__((weak)) BNRemoteFolder** BNRemoteProjectGetFolders(BNRemoteProject* project, size_t* count) { return NULL; }
__attribute__((weak)) BNCollaborationPermission** BNRemoteProjectGetGroupPermissions(BNRemoteProject* project, size_t* count) { return NULL; }
__attribute__((weak)) char* BNRemoteProjectGetId(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) int64_t BNRemoteProjectGetLastModified(BNRemoteProject* project) { return 0; }
__attribute__((weak)) char* BNRemoteProjectGetName(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) BNCollaborationPermission* BNRemoteProjectGetPermissionById(BNRemoteProject* project, const char* id) { return NULL; }
__attribute__((weak)) uint64_t BNRemoteProjectGetReceivedFileCount(BNRemoteProject* project) { return 0; }
__attribute__((weak)) uint64_t BNRemoteProjectGetReceivedFolderCount(BNRemoteProject* project) { return 0; }
__attribute__((weak)) BNRemote* BNRemoteProjectGetRemote(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) char* BNRemoteProjectGetUrl(BNRemoteProject* project) { return NULL; }
__attribute__((weak)) BNCollaborationPermission** BNRemoteProjectGetUserPermissions(BNRemoteProject* project, size_t* count) { return NULL; }
__attribute__((weak)) bool BNRemoteProjectHasPulledFiles(BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteProjectHasPulledGroupPermissions(BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteProjectHasPulledUserPermissions(BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteProjectIsAdmin(BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteProjectIsOpen(BNRemoteProject* project) { return false; }
__attribute__((weak)) bool BNRemoteProjectOpen(BNRemoteProject* project, BNProgressFunction progress, void* progressCtxt) { return false; }
__attribute__((weak)) bool BNRemoteProjectPullFiles(BNRemoteProject* project, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemoteProjectPullFolders(BNRemoteProject* project, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemoteProjectPullGroupPermissions(BNRemoteProject* project, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemoteProjectPullUserPermissions(BNRemoteProject* project, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemoteProjectPushFile(BNRemoteProject* project, BNRemoteFile* file, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) bool BNRemoteProjectPushFolder(BNRemoteProject* project, BNRemoteFolder* folder, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) bool BNRemoteProjectPushPermission(BNRemoteProject* project, BNCollaborationPermission* permission, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) bool BNRemoteProjectSetDescription(BNRemoteProject* project, const char* description) { return false; }
__attribute__((weak)) bool BNRemoteProjectSetName(BNRemoteProject* project, const char* name) { return false; }
__attribute__((weak)) bool BNRemotePullGroups(BNRemote* remote, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemotePullProjects(BNRemote* remote, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemotePullUsers(BNRemote* remote, BNProgressFunction progress, void* progressContext) { return false; }
__attribute__((weak)) bool BNRemotePushGroup(BNRemote* remote, BNCollaborationGroup* group, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) bool BNRemotePushProject(BNRemote* remote, BNRemoteProject* project, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) bool BNRemotePushUser(BNRemote* remote, BNCollaborationUser* user, const char** extraFieldKeys, const char** extraFieldValues, size_t extraFieldCount) { return false; }
__attribute__((weak)) int BNRemoteRequest(BNRemote* remote, void* request, void* ret) { return 0; }
__attribute__((weak)) char* BNRemoteRequestAuthenticationToken(BNRemote* remote, const char* username, const char* password) { return NULL; }
__attribute__((weak)) bool BNRemoteSearchGroups(BNRemote* remote, const char* prefix, uint64_t** groupIds, char*** groupNames, size_t* count) { return false; }
__attribute__((weak)) bool BNRemoteSearchUsers(BNRemote* remote, const char* prefix, char*** userIds, char*** usernames, size_t* count) { return false; }
__attribute__((weak)) char* BNTypeArchiveMergeConflictGetBaseSnapshotId(BNTypeArchiveMergeConflict* conflict) { return NULL; }
__attribute__((weak)) char* BNTypeArchiveMergeConflictGetFirstSnapshotId(BNTypeArchiveMergeConflict* conflict) { return NULL; }
__attribute__((weak)) char* BNTypeArchiveMergeConflictGetSecondSnapshotId(BNTypeArchiveMergeConflict* conflict) { return NULL; }
__attribute__((weak)) BNTypeArchive* BNTypeArchiveMergeConflictGetTypeArchive(BNTypeArchiveMergeConflict* conflict) { return NULL; }
__attribute__((weak)) char* BNTypeArchiveMergeConflictGetTypeId(BNTypeArchiveMergeConflict* conflict) { return NULL; }
__attribute__((weak)) bool BNTypeArchiveMergeConflictSuccess(BNTypeArchiveMergeConflict* conflict, const char* value) { return false; }
__attribute__((weak)) void BNUnregisterEnterpriseServerNotification(BNEnterpriseServerCallbacks* notify) { (void)0; }
__attribute__((weak)) bool BNUpdateEnterpriseServerLicense(uint64_t timeout) { return false; }

} // extern "C"
