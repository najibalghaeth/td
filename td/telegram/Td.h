//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdCallback.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TdParameters.h"

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/Timeout.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <memory>
#include <unordered_set>
#include <utility>

namespace td {
class AnimationsManager;
class AudiosManager;
class AuthManager;
class CallManager;
class CallbackQueriesManager;
class ChangePhoneNumberManager;
class ConfigManager;
class ContactsManager;
class DeviceTokenManager;
class DocumentsManager;
class FileManager;
class InlineQueriesManager;
class HashtagHints;
class MessagesManager;
class NetStatsManager;
class PasswordManager;
class PrivacyManager;
class SecretChatsManager;
class StickersManager;
class StorageManager;
class TopDialogManager;
class UpdatesManager;
class VideoNotesManager;
class VideosManager;
class VoiceNotesManager;
class WebPagesManager;

template <class T>
class Promise;
}  // namespace td

namespace td {

// Td may start closing after explicit "close" or "destroy" query.
// Or it may start closing by itself, because authorization is lost.
// It any case the parent will be notified via updateAuthorizationState.
//
// Td needs a way to know that it will receive no more queries.
// It happens after "hangup".
//
// Parent needs a way to know that it will receive no more updates.
// It happens after destruction of callback or after on_closed.
class Td final : public NetQueryCallback {
 public:
  Td(const Td &) = delete;
  Td(Td &&) = delete;
  Td &operator=(const Td &) = delete;
  Td &operator=(Td &&) = delete;

  explicit Td(unique_ptr<TdCallback> callback);

  void request(uint64 id, tl_object_ptr<td_api::Function> function);

  void destroy();
  void close();

  void update_qts(int32 qts);

  void force_get_difference();

  void on_result(NetQueryPtr query) override;
  void on_connection_state_changed(StateManager::State new_state);
  void on_authorization_lost();

  void on_online_updated(bool force, bool send_update);

  void on_channel_unban_timeout(int64 channel_id_long);

  bool is_online() const;

  template <class ActorT, class... ArgsT>
  ActorId<ActorT> create_net_actor(ArgsT &&... args) {
    auto slot_id = request_actors_.create(ActorOwn<>(), RequestActorIdType);
    inc_request_actor_refcnt();
    auto actor = make_unique<ActorT>(std::forward<ArgsT>(args)...);
    actor->set_parent(actor_shared(this, slot_id));

    auto actor_own = register_actor("net_actor", std::move(actor));
    auto actor_id = actor_own.get();
    *request_actors_.get(slot_id) = std::move(actor_own);
    return actor_id;
  }

  unique_ptr<AudiosManager> audios_manager_;
  unique_ptr<CallbackQueriesManager> callback_queries_manager_;
  unique_ptr<DocumentsManager> documents_manager_;
  unique_ptr<VideoNotesManager> video_notes_manager_;
  unique_ptr<VideosManager> videos_manager_;
  unique_ptr<VoiceNotesManager> voice_notes_manager_;

  std::unique_ptr<AnimationsManager> animations_manager_;
  ActorOwn<AnimationsManager> animations_manager_actor_;
  std::unique_ptr<AuthManager> auth_manager_;
  ActorOwn<AuthManager> auth_manager_actor_;
  std::unique_ptr<ChangePhoneNumberManager> change_phone_number_manager_;
  ActorOwn<ChangePhoneNumberManager> change_phone_number_manager_actor_;
  std::unique_ptr<ContactsManager> contacts_manager_;
  ActorOwn<ContactsManager> contacts_manager_actor_;
  std::unique_ptr<FileManager> file_manager_;
  ActorOwn<FileManager> file_manager_actor_;
  std::unique_ptr<InlineQueriesManager> inline_queries_manager_;
  ActorOwn<InlineQueriesManager> inline_queries_manager_actor_;
  std::unique_ptr<MessagesManager> messages_manager_;
  ActorOwn<MessagesManager> messages_manager_actor_;
  std::unique_ptr<StickersManager> stickers_manager_;
  ActorOwn<StickersManager> stickers_manager_actor_;
  std::unique_ptr<UpdatesManager> updates_manager_;
  ActorOwn<UpdatesManager> updates_manager_actor_;
  std::unique_ptr<WebPagesManager> web_pages_manager_;
  ActorOwn<WebPagesManager> web_pages_manager_actor_;

  ActorOwn<CallManager> call_manager_;
  ActorOwn<ConfigManager> config_manager_;
  ActorOwn<DeviceTokenManager> device_token_manager_;
  ActorOwn<HashtagHints> hashtag_hints_;
  ActorOwn<NetStatsManager> net_stats_manager_;
  ActorOwn<PasswordManager> password_manager_;
  ActorOwn<PrivacyManager> privacy_manager_;
  ActorOwn<SecretChatsManager> secret_chats_manager_;
  ActorOwn<StateManager> state_manager_;
  ActorOwn<StorageManager> storage_manager_;
  ActorOwn<TopDialogManager> top_dialog_manager_;

  class ResultHandler : public std::enable_shared_from_this<ResultHandler> {
   public:
    ResultHandler() = default;
    ResultHandler(const ResultHandler &) = delete;
    ResultHandler &operator=(const ResultHandler &) = delete;
    virtual ~ResultHandler() = default;
    virtual void on_result(NetQueryPtr query);
    virtual void on_result(uint64 id, BufferSlice packet) {
      UNREACHABLE();
    }
    virtual void on_error(uint64 id, Status status) {
      UNREACHABLE();
    }

    friend class Td;

   protected:
    void send_query(NetQueryPtr);

    Td *td = nullptr;

   private:
    void set_td(Td *new_td);
  };

  template <class HandlerT, class... Args>
  std::shared_ptr<HandlerT> create_handler(Args &&... args) {
    auto ptr = std::make_shared<HandlerT>(std::forward<Args>(args)...);
    ptr->set_td(this);
    return ptr;
  }

  void send_update(tl_object_ptr<td_api::Update> &&object);

  ActorShared<Td> create_reference();

  static td_api::object_ptr<td_api::Object> static_request(td_api::object_ptr<td_api::Function> function);

 private:
  static constexpr const char *tdlib_version = "1.1.0";
  static constexpr int32 ONLINE_TIMEOUT = 240;

  void send_result(uint64 id, tl_object_ptr<td_api::Object> object);
  void send_error(uint64 id, Status error);
  void send_error_impl(uint64 id, tl_object_ptr<td_api::error> error);
  void send_error_raw(uint64 id, int32 code, CSlice error);
  void answer_ok_query(uint64 id, Status status);

  void inc_actor_refcnt();
  void dec_actor_refcnt();

  void inc_request_actor_refcnt();
  void dec_request_actor_refcnt();

  void dec_stop_cnt();

  TdParameters parameters_;

  unique_ptr<TdCallback> callback_;

  StateManager::State connection_state_;

  std::unordered_multiset<uint64> request_set_;
  int actor_refcnt_ = 0;
  int request_actor_refcnt_ = 0;
  int stop_cnt_ = 2;
  bool destroy_flag_ = false;
  int close_flag_ = 0;

  enum class State { WaitParameters, Decrypt, Run, Close } state_ = State::WaitParameters;
  EncryptionInfo encryption_info_;

  vector<std::pair<uint64, std::shared_ptr<ResultHandler>>> result_handlers_;
  enum : int8 { RequestActorIdType = 1, ActorIdType = 2 };
  Container<ActorOwn<Actor>> request_actors_;

  bool is_online_ = false;
  MultiTimeout alarm_timeout_;

  static void on_alarm_timeout_callback(void *td_ptr, int64 request_id);
  void on_alarm_timeout(int64 request_id);

  template <class T>
  friend class RequestActor;              // uses send_result/send_error
  friend class TestQuery;                 // uses send_result/send_error
  friend class AuthManager;               // uses send_result/send_error
  friend class ChangePhoneNumberManager;  // uses send_result/send_error

  void add_handler(uint64 id, std::shared_ptr<ResultHandler> handler);
  std::shared_ptr<ResultHandler> extract_handler(uint64 id);
  void invalidate_handler(ResultHandler *handler);
  void clear_handlers();
  // void destroy_handler(ResultHandler *handler);

  void on_config_option_updated(const string &name);

  static tl_object_ptr<td_api::ConnectionState> get_connection_state_object(StateManager::State state);

  void send(NetQueryPtr &&query);

  class OnRequest;

  class DownloadFileCallback;

  std::shared_ptr<DownloadFileCallback> download_file_callback_;

  class UploadFileCallback;

  std::shared_ptr<UploadFileCallback> upload_file_callback_;

  template <class T>
  auto create_request_promise(uint64 id) {
    return PromiseCreator::lambda([id = id, actor_id = actor_id(this)](Result<T> r_state) {
      if (r_state.is_error()) {
        send_closure(actor_id, &Td::send_error, id, r_state.move_as_error());
      } else {
        send_closure(actor_id, &Td::send_result, id, r_state.move_as_ok());
      }
    });
  }

  template <class T>
  void on_request(uint64 id, const T &request) = delete;

  void on_request(uint64 id, const td_api::setTdlibParameters &request);

  void on_request(uint64 id, const td_api::checkDatabaseEncryptionKey &request);

  void on_request(uint64 id, td_api::setDatabaseEncryptionKey &request);

  void on_request(uint64 id, const td_api::getAuthorizationState &request);

  void on_request(uint64 id, td_api::setAuthenticationPhoneNumber &request);

  void on_request(uint64 id, const td_api::resendAuthenticationCode &request);

  void on_request(uint64 id, td_api::checkAuthenticationCode &request);

  void on_request(uint64 id, td_api::checkAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::requestAuthenticationPasswordRecovery &request);

  void on_request(uint64 id, td_api::recoverAuthenticationPassword &request);

  void on_request(uint64 id, const td_api::logOut &request);

  void on_request(uint64 id, const td_api::close &request);

  void on_request(uint64 id, const td_api::destroy &request);

  void on_request(uint64 id, td_api::checkAuthenticationBotToken &request);

  void on_request(uint64 id, td_api::getPasswordState &request);

  void on_request(uint64 id, td_api::setPassword &request);

  void on_request(uint64 id, td_api::getRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::setRecoveryEmailAddress &request);

  void on_request(uint64 id, td_api::requestPasswordRecovery &request);

  void on_request(uint64 id, td_api::recoverPassword &request);

  void on_request(uint64 id, td_api::getTemporaryPasswordState &request);

  void on_request(uint64 id, td_api::createTemporaryPassword &request);

  void on_request(uint64 id, td_api::processDcUpdate &request);

  void on_request(uint64 id, td_api::registerDevice &request);

  void on_request(uint64 id, td_api::getUserPrivacySettingRules &request);

  void on_request(uint64 id, td_api::setUserPrivacySettingRules &request);

  void on_request(uint64 id, const td_api::getAccountTtl &request);

  void on_request(uint64 id, const td_api::setAccountTtl &request);

  void on_request(uint64 id, td_api::deleteAccount &request);

  void on_request(uint64 id, td_api::changePhoneNumber &request);

  void on_request(uint64 id, td_api::checkChangePhoneNumberCode &request);

  void on_request(uint64 id, td_api::resendChangePhoneNumberCode &request);

  void on_request(uint64 id, const td_api::getActiveSessions &request);

  void on_request(uint64 id, const td_api::terminateSession &request);

  void on_request(uint64 id, const td_api::terminateAllOtherSessions &request);

  void on_request(uint64 id, const td_api::getMe &request);

  void on_request(uint64 id, const td_api::getUser &request);

  void on_request(uint64 id, const td_api::getUserFullInfo &request);

  void on_request(uint64 id, const td_api::getBasicGroup &request);

  void on_request(uint64 id, const td_api::getBasicGroupFullInfo &request);

  void on_request(uint64 id, const td_api::getSupergroup &request);

  void on_request(uint64 id, const td_api::getSupergroupFullInfo &request);

  void on_request(uint64 id, const td_api::getSecretChat &request);

  void on_request(uint64 id, const td_api::getChat &request);

  void on_request(uint64 id, const td_api::getMessage &request);

  void on_request(uint64 id, const td_api::getMessages &request);

  void on_request(uint64 id, const td_api::getPublicMessageLink &request);

  void on_request(uint64 id, const td_api::getFile &request);

  void on_request(uint64 id, td_api::getRemoteFile &request);

  void on_request(uint64 id, td_api::getStorageStatistics &request);

  void on_request(uint64 id, td_api::getStorageStatisticsFast &request);

  void on_request(uint64 id, td_api::optimizeStorage &request);

  void on_request(uint64 id, td_api::getNetworkStatistics &request);

  void on_request(uint64 id, td_api::resetNetworkStatistics &request);

  void on_request(uint64 id, td_api::addNetworkStatistics &request);

  void on_request(uint64 id, td_api::setNetworkType &request);

  void on_request(uint64 id, td_api::getTopChats &request);

  void on_request(uint64 id, const td_api::removeTopChat &request);

  void on_request(uint64 id, const td_api::getChats &request);

  void on_request(uint64 id, td_api::searchPublicChat &request);

  void on_request(uint64 id, td_api::searchPublicChats &request);

  void on_request(uint64 id, td_api::searchChats &request);

  void on_request(uint64 id, td_api::searchChatsOnServer &request);

  void on_request(uint64 id, const td_api::addRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::removeRecentlyFoundChat &request);

  void on_request(uint64 id, const td_api::clearRecentlyFoundChats &request);

  void on_request(uint64 id, const td_api::getGroupsInCommon &request);

  void on_request(uint64 id, const td_api::getCreatedPublicChats &request);

  void on_request(uint64 id, const td_api::openChat &request);

  void on_request(uint64 id, const td_api::closeChat &request);

  void on_request(uint64 id, const td_api::viewMessages &request);

  void on_request(uint64 id, const td_api::openMessageContent &request);

  void on_request(uint64 id, const td_api::getChatHistory &request);

  void on_request(uint64 id, const td_api::deleteChatHistory &request);

  void on_request(uint64 id, td_api::searchChatMessages &request);

  void on_request(uint64 id, td_api::searchSecretMessages &request);

  void on_request(uint64 id, td_api::searchMessages &request);

  void on_request(uint64 id, td_api::searchCallMessages &request);

  void on_request(uint64 id, const td_api::searchChatRecentLocationMessages &request);

  void on_request(uint64 id, const td_api::getActiveLiveLocationMessages &request);

  void on_request(uint64 id, const td_api::getChatMessageByDate &request);

  void on_request(uint64 id, const td_api::deleteMessages &request);

  void on_request(uint64 id, const td_api::deleteChatMessagesFromUser &request);

  void on_request(uint64 id, const td_api::readAllChatMentions &request);

  void on_request(uint64 id, td_api::sendMessage &request);

  void on_request(uint64 id, td_api::sendMessageAlbum &request);

  void on_request(uint64 id, td_api::sendBotStartMessage &request);

  void on_request(uint64 id, td_api::sendInlineQueryResultMessage &request);

  void on_request(uint64 id, const td_api::sendChatSetTtlMessage &request);

  void on_request(uint64 id, td_api::editMessageText &request);

  void on_request(uint64 id, td_api::editMessageLiveLocation &request);

  void on_request(uint64 id, td_api::editMessageCaption &request);

  void on_request(uint64 id, td_api::editMessageReplyMarkup &request);

  void on_request(uint64 id, td_api::editInlineMessageText &request);

  void on_request(uint64 id, td_api::editInlineMessageLiveLocation &request);

  void on_request(uint64 id, td_api::editInlineMessageCaption &request);

  void on_request(uint64 id, td_api::editInlineMessageReplyMarkup &request);

  void on_request(uint64 id, td_api::setGameScore &request);

  void on_request(uint64 id, td_api::setInlineGameScore &request);

  void on_request(uint64 id, td_api::getGameHighScores &request);

  void on_request(uint64 id, td_api::getInlineGameHighScores &request);

  void on_request(uint64 id, const td_api::deleteChatReplyMarkup &request);

  void on_request(uint64 id, td_api::sendChatAction &request);

  void on_request(uint64 id, td_api::sendChatScreenshotTakenNotification &request);

  void on_request(uint64 id, const td_api::forwardMessages &request);

  void on_request(uint64 id, td_api::getWebPagePreview &request);

  void on_request(uint64 id, td_api::getWebPageInstantView &request);

  void on_request(uint64 id, const td_api::createPrivateChat &request);

  void on_request(uint64 id, const td_api::createBasicGroupChat &request);

  void on_request(uint64 id, const td_api::createSupergroupChat &request);

  void on_request(uint64 id, td_api::createSecretChat &request);

  void on_request(uint64 id, td_api::createNewBasicGroupChat &request);

  void on_request(uint64 id, td_api::createNewSupergroupChat &request);

  void on_request(uint64 id, td_api::createNewSecretChat &request);

  void on_request(uint64 id, td_api::createCall &request);

  void on_request(uint64 id, td_api::discardCall &request);

  void on_request(uint64 id, td_api::acceptCall &request);

  void on_request(uint64 id, td_api::sendCallRating &request);

  void on_request(uint64 id, td_api::sendCallDebugInformation &request);

  void on_request(uint64 id, const td_api::upgradeBasicGroupChatToSupergroupChat &request);

  void on_request(uint64 id, td_api::setChatTitle &request);

  void on_request(uint64 id, td_api::setChatPhoto &request);

  void on_request(uint64 id, td_api::setChatDraftMessage &request);

  void on_request(uint64 id, const td_api::toggleChatIsPinned &request);

  void on_request(uint64 id, const td_api::setPinnedChats &request);

  void on_request(uint64 id, td_api::setChatClientData &request);

  void on_request(uint64 id, const td_api::addChatMember &request);

  void on_request(uint64 id, const td_api::addChatMembers &request);

  void on_request(uint64 id, td_api::setChatMemberStatus &request);

  void on_request(uint64 id, const td_api::getChatMember &request);

  void on_request(uint64 id, td_api::searchChatMembers &request);

  void on_request(uint64 id, td_api::getChatAdministrators &request);

  void on_request(uint64 id, const td_api::generateChatInviteLink &request);

  void on_request(uint64 id, td_api::checkChatInviteLink &request);

  void on_request(uint64 id, td_api::joinChatByInviteLink &request);

  void on_request(uint64 id, td_api::getChatEventLog &request);

  void on_request(uint64 id, const td_api::downloadFile &request);

  void on_request(uint64 id, const td_api::cancelDownloadFile &request);

  void on_request(uint64 id, td_api::uploadFile &request);

  void on_request(uint64 id, const td_api::cancelUploadFile &request);

  void on_request(uint64 id, const td_api::setFileGenerationProgress &request);

  void on_request(uint64 id, td_api::finishFileGeneration &request);

  void on_request(uint64 id, const td_api::deleteFile &request);

  void on_request(uint64 id, const td_api::blockUser &request);

  void on_request(uint64 id, const td_api::unblockUser &request);

  void on_request(uint64 id, const td_api::getBlockedUsers &request);

  void on_request(uint64 id, td_api::importContacts &request);

  void on_request(uint64 id, td_api::searchContacts &request);

  void on_request(uint64 id, td_api::removeContacts &request);

  void on_request(uint64 id, const td_api::getImportedContactCount &request);

  void on_request(uint64 id, td_api::changeImportedContacts &request);

  void on_request(uint64 id, const td_api::clearImportedContacts &request);

  void on_request(uint64 id, const td_api::getRecentInlineBots &request);

  void on_request(uint64 id, td_api::setName &request);

  void on_request(uint64 id, td_api::setBio &request);

  void on_request(uint64 id, td_api::setUsername &request);

  void on_request(uint64 id, td_api::setProfilePhoto &request);

  void on_request(uint64 id, const td_api::deleteProfilePhoto &request);

  void on_request(uint64 id, const td_api::getUserProfilePhotos &request);

  void on_request(uint64 id, const td_api::toggleBasicGroupAdministrators &request);

  void on_request(uint64 id, td_api::setSupergroupUsername &request);

  void on_request(uint64 id, const td_api::setSupergroupStickerSet &request);

  void on_request(uint64 id, const td_api::toggleSupergroupInvites &request);

  void on_request(uint64 id, const td_api::toggleSupergroupSignMessages &request);

  void on_request(uint64 id, const td_api::toggleSupergroupIsAllHistoryAvailable &request);

  void on_request(uint64 id, td_api::setSupergroupDescription &request);

  void on_request(uint64 id, const td_api::pinSupergroupMessage &request);

  void on_request(uint64 id, const td_api::unpinSupergroupMessage &request);

  void on_request(uint64 id, const td_api::reportSupergroupSpam &request);

  void on_request(uint64 id, td_api::getSupergroupMembers &request);

  void on_request(uint64 id, const td_api::deleteSupergroup &request);

  void on_request(uint64 id, td_api::closeSecretChat &request);

  void on_request(uint64 id, td_api::getStickers &request);

  void on_request(uint64 id, const td_api::getInstalledStickerSets &request);

  void on_request(uint64 id, const td_api::getArchivedStickerSets &request);

  void on_request(uint64 id, const td_api::getTrendingStickerSets &request);

  void on_request(uint64 id, const td_api::getAttachedStickerSets &request);

  void on_request(uint64 id, const td_api::getStickerSet &request);

  void on_request(uint64 id, td_api::searchStickerSet &request);

  void on_request(uint64 id, const td_api::changeStickerSet &request);

  void on_request(uint64 id, const td_api::viewTrendingStickerSets &request);

  void on_request(uint64 id, td_api::reorderInstalledStickerSets &request);

  void on_request(uint64 id, td_api::uploadStickerFile &request);

  void on_request(uint64 id, td_api::createNewStickerSet &request);

  void on_request(uint64 id, td_api::addStickerToSet &request);

  void on_request(uint64 id, td_api::setStickerPositionInSet &request);

  void on_request(uint64 id, td_api::removeStickerFromSet &request);

  void on_request(uint64 id, const td_api::getRecentStickers &request);

  void on_request(uint64 id, td_api::addRecentSticker &request);

  void on_request(uint64 id, td_api::removeRecentSticker &request);

  void on_request(uint64 id, td_api::clearRecentStickers &request);

  void on_request(uint64 id, const td_api::getSavedAnimations &request);

  void on_request(uint64 id, td_api::addSavedAnimation &request);

  void on_request(uint64 id, td_api::removeSavedAnimation &request);

  void on_request(uint64 id, td_api::getStickerEmojis &request);

  void on_request(uint64 id, const td_api::getFavoriteStickers &request);

  void on_request(uint64 id, td_api::addFavoriteSticker &request);

  void on_request(uint64 id, td_api::removeFavoriteSticker &request);

  void on_request(uint64 id, const td_api::getNotificationSettings &request);

  void on_request(uint64 id, td_api::setNotificationSettings &request);

  void on_request(uint64 id, const td_api::resetAllNotificationSettings &request);

  void on_request(uint64 id, const td_api::getChatReportSpamState &request);

  void on_request(uint64 id, const td_api::changeChatReportSpamState &request);

  void on_request(uint64 id, td_api::reportChat &request);

  void on_request(uint64 id, td_api::getOption &request);

  void on_request(uint64 id, td_api::setOption &request);

  void on_request(uint64 id, td_api::getInlineQueryResults &request);

  void on_request(uint64 id, td_api::answerInlineQuery &request);

  void on_request(uint64 id, td_api::getCallbackQueryAnswer &request);

  void on_request(uint64 id, td_api::answerCallbackQuery &request);

  void on_request(uint64 id, td_api::answerShippingQuery &request);

  void on_request(uint64 id, td_api::answerPreCheckoutQuery &request);

  void on_request(uint64 id, const td_api::getPaymentForm &request);

  void on_request(uint64 id, td_api::validateOrderInfo &request);

  void on_request(uint64 id, td_api::sendPaymentForm &request);

  void on_request(uint64 id, const td_api::getPaymentReceipt &request);

  void on_request(uint64 id, const td_api::getSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedOrderInfo &request);

  void on_request(uint64 id, const td_api::deleteSavedCredentials &request);

  void on_request(uint64 id, const td_api::getSupportUser &request);

  void on_request(uint64 id, const td_api::getWallpapers &request);

  void on_request(uint64 id, td_api::getRecentlyVisitedTMeUrls &request);

  void on_request(uint64 id, td_api::setBotUpdatesStatus &request);

  void on_request(uint64 id, td_api::sendCustomRequest &request);

  void on_request(uint64 id, td_api::answerCustomQuery &request);

  void on_request(uint64 id, const td_api::setAlarm &request);

  void on_request(uint64 id, td_api::searchHashtags &request);

  void on_request(uint64 id, td_api::removeRecentHashtag &request);

  void on_request(uint64 id, const td_api::getInviteText &request);

  void on_request(uint64 id, const td_api::getTermsOfService &request);

  void on_request(uint64 id, const td_api::getProxy &request);

  void on_request(uint64 id, const td_api::setProxy &request);

  void on_request(uint64 id, const td_api::getTextEntities &request);

  void on_request(uint64 id, td_api::parseTextEntities &request);

  void on_request(uint64 id, const td_api::getFileMimeType &request);

  void on_request(uint64 id, const td_api::getFileExtension &request);

  // test
  void on_request(uint64 id, td_api::testNetwork &request);
  void on_request(uint64 id, td_api::testGetDifference &request);
  void on_request(uint64 id, td_api::testUseUpdate &request);
  void on_request(uint64 id, td_api::testUseError &request);
  void on_request(uint64 id, td_api::testCallEmpty &request);
  void on_request(uint64 id, td_api::testSquareInt &request);
  void on_request(uint64 id, td_api::testCallString &request);
  void on_request(uint64 id, td_api::testCallBytes &request);
  void on_request(uint64 id, td_api::testCallVectorInt &request);
  void on_request(uint64 id, td_api::testCallVectorIntObject &request);
  void on_request(uint64 id, td_api::testCallVectorString &request);
  void on_request(uint64 id, td_api::testCallVectorStringObject &request);

  template <class T>
  static td_api::object_ptr<td_api::Object> do_static_request(const T &);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(td_api::parseTextEntities &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileMimeType &request);
  static td_api::object_ptr<td_api::Object> do_static_request(const td_api::getFileExtension &request);

  Status init(DbKey key) TD_WARN_UNUSED_RESULT;
  void clear();
  void close_impl(bool destroy_flag);
  Status fix_parameters(TdParameters &parameters) TD_WARN_UNUSED_RESULT;
  Status set_td_parameters(td_api::object_ptr<td_api::tdlibParameters> parameters) TD_WARN_UNUSED_RESULT;

  // Actor
  void start_up() override;
  void tear_down() override;
  void hangup_shared() override;
  void hangup() override;
};

}  // namespace td
