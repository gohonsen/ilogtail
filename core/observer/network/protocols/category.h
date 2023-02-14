#pragma once
#include <ostream>
#include "interface/network.h"
#include "interface/global.h"
#include "interface/helper.h"
#include "common.h"
#include "Logger.h"
#include "xxhash/xxhash.h"

namespace logtail {


/**
 *   Common hash key for metrics aggregation
 */
struct CommonAggKey {
    CommonAggKey() = default;
    CommonAggKey(const CommonAggKey& other) = default;
    CommonAggKey(CommonAggKey&& other) noexcept
        : HashVal(other.HashVal),
          ConnId(other.ConnId),
          RemotePort(other.RemotePort),
          LocalPort(other.LocalPort),
          Role(other.Role),
          RemoteIp(std::move(other.RemoteIp)),
          LocalIp(std::move(other.LocalIp)),
          CommonTags(std::move(other.CommonTags)) {}
    CommonAggKey& operator=(const CommonAggKey& other) = default;
    CommonAggKey& operator=(CommonAggKey&& other) noexcept {
        this->HashVal = other.HashVal;
        this->Role = other.Role;
        this->RemotePort = other.RemotePort;
        this->RemoteIp = std::move(other.RemoteIp);
        this->LocalPort = other.LocalPort;
        this->LocalIp = std::move(other.LocalIp);
        this->ConnId = other.ConnId;
        this->CommonTags=std::move(other.CommonTags);
        return *this;
    }
    explicit CommonAggKey(PacketEventHeader* header)
        : HashVal(header->SockHash),
          ConnId(GenConnectionID(header->PID, header->SockHash)),
          RemotePort(header->DstPort),
          LocalPort(header->SrcPort),
          Pid(header->PID),
          Role(header->RoleType),
          RemoteIp(SockAddressToString(header->DstAddr)),
          LocalIp(SockAddressToString(header->SrcAddr)) {
        HashVal = XXH32(&this->Role, sizeof(Role), HashVal);
        CommonTags.Reserve(6);
        AddAnyLogContent(CommonTags.Add(), observer::kRole, PacketRoleTypeToString(Role));
        AddAnyLogContent(CommonTags.Add(), observer::kRemoteAddr, RemoteIp);
        AddAnyLogContent(CommonTags.Add(), observer::kRemotePort, RemotePort);
        AddAnyLogContent(CommonTags.Add(), observer::kLocalPort, LocalPort);
        AddAnyLogContent(CommonTags.Add(), observer::kLocalAddr, LocalIp);
        AddAnyLogContent(CommonTags.Add(), observer::kConnId, ConnId);
    }

    friend std::ostream& operator<<(std::ostream& Os, const CommonAggKey& Key) {
        Os << "HashVal: " << Key.HashVal << " ConnId: " << Key.ConnId << " RemotePort: " << Key.RemotePort
           << " LocalPort: " << Key.LocalPort << " Role: " << PacketRoleTypeToString(Key.Role)
           << " RemoteIp: " << Key.RemoteIp << " LocalIp: " << Key.LocalIp;
        return Os;
    }

    std::string ToString() const {
        std::stringstream ss;
        ss << *this;
        return ss.str();
    }


    void ToPB(::google::protobuf::RepeatedPtrField<sls_logs::Log_Content>& contents) const {
        static ServiceMetaManager* sHostnameManager = logtail::ServiceMetaManager::GetInstance();
        contents.CopyFrom(CommonTags);
        const ServiceMeta& meta = sHostnameManager->GetServiceMeta(this->Pid, this->RemoteIp);
        auto remoteInfo = std::string(kRemoteInfoPrefix)
                              .append(meta.Empty() ? this->RemoteIp : meta.Host)
                              .append(kRemoteInfoSuffix);
        AddAnyLogContent(contents.Add(), observer::kRemoteInfo, std::move(remoteInfo));
    }

    void ToPB(sls_logs::Log* log) const {
        auto contents = log->contents();
        ToPB(contents);
    }


    uint64_t HashVal{0};
    uint64_t ConnId{0};
    uint16_t RemotePort{0};
    uint16_t LocalPort{0};
    uint16_t Pid{0};
    PacketRoleType Role{PacketRoleType::Unknown};
    std::string RemoteIp;
    std::string LocalIp;
    google::protobuf::RepeatedPtrField<sls_logs::Log_Content> CommonTags;
};

template <ProtocolType PT>
struct DBAggKey {
    DBAggKey() = default;
    DBAggKey(const DBAggKey& other) = default;
    DBAggKey(DBAggKey&& other) noexcept
        : ConnKey(std::move(other.ConnKey)),
          QueryCmd(std::move(other.QueryCmd)),
          Query(std::move(other.Query)),
          Version(std::move(other.Version)),
          Status(other.Status) {}
    DBAggKey& operator=(const DBAggKey& other) = default;
    DBAggKey& operator=(DBAggKey&& other) noexcept {
        this->ConnKey = std::move(other.ConnKey);
        this->QueryCmd = std::move(other.QueryCmd);
        this->Query = std::move(other.Query);
        this->Version = std::move(other.Version);
        this->Status = other.Status;
        return *this;
    }

    uint64_t Hash() const {
        uint64_t hashValue = ConnKey.HashVal;
        hashValue = XXH32(this->QueryCmd.c_str(), this->QueryCmd.size(), hashValue);
        hashValue = XXH32(this->Query.c_str(), this->Query.size(), hashValue);
        hashValue = XXH32(this->Version.c_str(), this->Version.size(), hashValue);
        hashValue = XXH32(&this->Status, sizeof(this->Status), hashValue);
        return hashValue;
    }
    void ToPB(sls_logs::Log* log) const {
        AddAnyLogContent(log, observer::kVersion, Version);
        AddAnyLogContent(log, observer::kQueryCmd, QueryCmd);
        AddAnyLogContent(log, observer::kQuery, Query);
        AddAnyLogContent(log, observer::kStatus, Status);
        AddAnyLogContent(log, observer::kProtocol, ProtocolTypeToString(PT));
        AddAnyLogContent(log, observer::kType, ObserverMetricsTypeToString(ObserverMetricsType::L7_DB_METRICS));
        ConnKey.ToPB(log);
    }

    std::string ProtocolType() { return ProtocolTypeToString(PT); }

    friend std::ostream& operator<<(std::ostream& Os, const DBAggKey& Key) {
        Os << "ConnKey: " << Key.ConnKey << " QueryCmd: " << Key.QueryCmd << " Query: " << Key.Query
           << " Version: " << Key.Version << " Status: " << Key.Status;
        return Os;
    }
    std::string ToString() const {
        std::stringstream ss;
        ss << *this;
        return ss.str();
    }

    CommonAggKey ConnKey;
    std::string QueryCmd;
    std::string Query;
    std::string Version;
    std::string Details; // used to stored serialized details
    int8_t Status{-1};
};

template <ProtocolType PT>
struct RequestAggKey {
    RequestAggKey() = default;
    RequestAggKey(const RequestAggKey& other) = default;
    RequestAggKey& operator=(const RequestAggKey& other) = default;
    RequestAggKey(RequestAggKey&& other) noexcept
        : ConnKey(std::move(other.ConnKey)),
          ReqType(std::move(other.ReqType)),
          ReqDomain(std::move(other.ReqDomain)),
          ReqResource(std::move(other.ReqResource)),
          Version(std::move(other.Version)),
          RespCode(other.RespCode),
          RespStatus(other.RespStatus) {}
    RequestAggKey& operator=(RequestAggKey&& other) noexcept {
        this->ConnKey = std::move(other.ConnKey);
        this->ReqType = std::move(other.ReqType);
        this->ReqDomain = std::move(other.ReqDomain);
        this->ReqResource = std::move(other.ReqResource);
        this->Version = std::move(other.Version);
        this->RespCode = other.RespCode;
        this->RespStatus = other.RespStatus;
        return *this;
    }

    uint64_t Hash() const {
        uint64_t hashValue = ConnKey.HashVal;
        hashValue = XXH32(this->ReqType.c_str(), this->ReqType.size(), hashValue);
        hashValue = XXH32(this->ReqDomain.c_str(), this->ReqDomain.size(), hashValue);
        hashValue = XXH32(this->ReqResource.c_str(), this->ReqResource.size(), hashValue);
        hashValue = XXH32(this->Version.c_str(), this->Version.size(), hashValue);
        hashValue = XXH32(&this->RespCode, sizeof(this->RespCode), hashValue);
        hashValue = XXH32(&this->RespStatus, sizeof(this->RespStatus), hashValue);
        return hashValue;
    }
    void ToPB(sls_logs::Log* log) const {
        AddAnyLogContent(log, observer::kReqType, ReqType);
        AddAnyLogContent(log, observer::kReqDomain, ReqDomain);
        AddAnyLogContent(log, observer::kReqResource, ReqResource);
        AddAnyLogContent(log, observer::kVersion, Version);
        AddAnyLogContent(log, observer::kRespStatus, RespStatus);
        AddAnyLogContent(log, observer::kRespCode, RespCode);
        AddAnyLogContent(log, observer::kProtocol, ProtocolTypeToString(PT));
        AddAnyLogContent(log, observer::kType, ObserverMetricsTypeToString(ObserverMetricsType::L7_REQ_METRICS));
        ConnKey.ToPB(log);
    }

    std::string ProtocolType() { return ProtocolTypeToString(PT); }

    friend std::ostream& operator<<(std::ostream& Os, const RequestAggKey& Key) {
        Os << "ConnKey: " << Key.ConnKey << " ReqType: " << Key.ReqType << " ReqDomain: " << Key.ReqDomain
           << " ReqResource: " << Key.ReqResource << " Version: " << Key.Version << " RespCode: " << Key.RespCode
           << " RespStatus: " << Key.RespStatus;
        return Os;
    }
    std::string ToString() const {
        std::stringstream ss;
        ss << *this;
        return ss.str();
    }

    CommonAggKey ConnKey;
    std::string ReqType;
    std::string ReqDomain;
    std::string ReqResource;
    std::string Version;
    int16_t RespCode{-1};
    int8_t RespStatus{-1};
};
} // namespace logtail
