# ESP32 Reticulum Gateway IPFS Integration Specification
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-IPFS

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for IPFS (InterPlanetary File System) integration in the ESP32 Reticulum Network Stack Gateway Node system. The integration provides content addressing and distributed storage capabilities within the Reticulum network.

### 1.2 Applicability
This specification applies to all IPFS operations including content fetching, publishing, and content addressing within the Reticulum network context.

---

## 2.0 IPFS INTEGRATION OVERVIEW

### 2.1 Architecture Approach
Due to ESP32 resource constraints, this implementation uses a **lightweight gateway client** approach rather than running a full IPFS node:

- **Full IPFS Node**: Requires significant RAM/CPU (not feasible on ESP32)
- **Gateway Client**: Fetches content from existing IPFS gateways via HTTP
- **Publishing**: Via local IPFS node HTTP API (if available)

### 2.2 Functional Capabilities
- **Content Fetching**: Retrieve content from IPFS via HTTP gateways
- **Content Addressing**: Use IPFS hashes for content references
- **Publishing**: Upload content to IPFS via local node API
- **Hash Management**: IPFS hash extraction and validation

---

## 3.0 GATEWAY CLIENT SPECIFICATIONS

### 3.1 Gateway Access Method
- **Protocol**: HTTP/HTTPS
- **Method**: GET requests
- **Response Format**: Binary content or JSON

### 3.2 Supported Gateways
- **Public Gateways**:
  - `https://ipfs.io/ipfs/` (Protocol Labs)
  - `https://gateway.pinata.cloud/ipfs/` (Pinata)
  - `https://dweb.link/ipfs/` (Protocol Labs)
  - `https://cloudflare-ipfs.com/ipfs/` (Cloudflare)

- **Private Gateways**: Custom gateway URLs supported

### 3.3 Configuration Parameters
```cpp
#define IPFS_GATEWAY_URL "https://ipfs.io/ipfs/"
#define IPFS_MAX_CONTENT_SIZE 10240  // 10 KB
#define IPFS_TIMEOUT_MS 10000        // 10 seconds
#define IPFS_CACHE_SIZE 5            // Cache entries
```

### 3.4 Operational Procedures

#### 3.4.1 Content Fetching Procedure
1. Construct URL: `gateway_url + ipfs_hash`
2. Initialize HTTP client
3. Send GET request
4. Receive response
5. Validate content size
6. Read content data
7. Return content to caller

#### 3.4.2 Usage Example
```cpp
std::vector<uint8_t> content;
String ipfsHash = "QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG";

if (interfaceManager.fetchIPFSContent(ipfsHash.c_str(), content)) {
    // Content fetched successfully
    DebugSerial.print("Fetched ");
    DebugSerial.print(content.size());
    DebugSerial.println(" bytes from IPFS");
    
    // Process content...
} else {
    DebugSerial.println("Failed to fetch IPFS content");
}
```

---

## 4.0 IPFS PUBLISHING SPECIFICATIONS

### 4.1 Publishing Architecture
- **Method**: HTTP API to local IPFS node
- **Endpoint**: `/api/v0/add`
- **Protocol**: HTTP POST
- **Content-Type**: `multipart/form-data`

### 4.2 Configuration Parameters
```cpp
#define IPFS_LOCAL_NODE_URL "http://localhost:5001"
#define IPFS_LOCAL_NODE_ENABLED 1
#define IPFS_PUBLISH_TIMEOUT_MS 30000  // 30 seconds
```

### 4.3 Publishing Procedure
1. Connect to local IPFS node API
2. Construct multipart form data
3. POST data to `/api/v0/add` endpoint
4. Receive JSON response
5. Parse response to extract hash
6. Return hash to caller

### 4.4 Response Format
```json
{
  "Name": "filename.ext",
  "Hash": "Qm...",
  "Size": "12345"
}
```

### 4.5 Usage Example
```cpp
String ipfsHash;
uint8_t data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"

if (interfaceManager.publishToIPFS(data, 5, ipfsHash)) {
    Serial.print("Published! Hash: ");
    Serial.println(ipfsHash);
}
```

---

## 5.0 CONTENT ADDRESSING IN RETICULUM

### 5.1 Integration Pattern
IPFS hashes can be embedded in Reticulum packets for content distribution:

```
Reticulum Packet
├── Header (destination, source, etc.)
└── Payload
    └── IPFS Hash (46 characters base58)
        └── Recipient fetches content from IPFS
```

### 5.2 Use Cases

#### 5.2.1 Firmware Updates
- Store firmware images on IPFS
- Reference by hash in update packets
- Nodes fetch and verify via hash

#### 5.2.2 Configuration Distribution
- Store network configurations on IPFS
- Nodes fetch configs by hash
- Ensures consistency

#### 5.2.3 Data Archival
- Store sensor data on IPFS
- Reference in Reticulum packets
- Long-term distributed storage

#### 5.2.4 Content Sharing
- Share files, images, documents
- Use IPFS hashes as references
- Distributed, censorship-resistant

---

## 6.0 SECURITY CONSIDERATIONS

### 6.1 Content Verification
- **Hash Verification**: IPFS hashes are content-addressed
- **Verification Procedure**: Fetch content and verify hash matches
- **Protection**: Protects against gateway tampering

### 6.2 Gateway Trust
- **Public Gateways**: May be untrusted
- **Recommendation**: Run your own gateway when possible
- **HTTPS**: Use HTTPS for gateway connections
- **Verification**: Always verify content hashes after fetching

### 6.3 Best Practices
1. Verify IPFS hash after fetching content
2. Use trusted gateways or run local gateway
3. Validate content size before processing
4. Implement timeout handling
5. Monitor for suspicious content

---

## 7.0 PERFORMANCE SPECIFICATIONS

### 7.1 Fetching Performance
- **Small Content (<1KB)**: 500-1000 ms (typical)
- **Medium Content (1-5KB)**: 1-3 seconds (typical)
- **Large Content (5-10KB)**: 3-10 seconds (typical)

### 7.2 Publishing Performance
- **Small Content (<1KB)**: 1-3 seconds (typical)
- **Medium Content (1-5KB)**: 3-10 seconds (typical)
- **Large Content (5-10KB)**: 10-30 seconds (typical)

### 7.3 Performance Factors
- Gateway response time
- Network latency
- Content size
- Gateway load
- Local network conditions

---

## 8.0 LIMITATIONS AND CONSTRAINTS

### 8.1 Current Limitations
1. **Read-Only Gateway Mode**: Can only fetch, not publish (without local node)
2. **Gateway Dependency**: Requires internet connection and gateway availability
3. **Size Limits**: Limited by `IPFS_MAX_CONTENT_SIZE` (default 10KB)
4. **No Caching**: Content is not cached between fetches
5. **No Local Node**: Does not run a local IPFS node

### 8.2 Resource Constraints
- **Memory**: Limited by ESP32 RAM (typically 320KB)
- **Storage**: No persistent IPFS storage
- **CPU**: HTTP requests are CPU-intensive
- **Network**: Requires WiFi connection

### 8.3 Gateway Limitations
- **Availability**: Public gateways may be unavailable
- **Rate Limiting**: Some gateways implement rate limits
- **Content Filtering**: Some gateways may filter content
- **Latency**: Variable depending on gateway location

---

## 9.0 TROUBLESHOOTING PROCEDURES

### 9.1 Gateway Timeout Issues

#### 9.1.1 Symptoms
- Fetch operations timeout
- No response from gateway

#### 9.1.2 Diagnosis
1. Check WiFi connection status
2. Verify gateway URL is accessible
3. Test gateway with browser or curl
4. Check for firewall restrictions

#### 9.1.3 Resolution
- Try different gateway URL
- Increase `IPFS_TIMEOUT_MS` if needed
- Verify gateway is accessible
- Check network connectivity

### 9.2 Content Too Large

#### 9.2.1 Symptoms
- Fetch fails with size error
- Memory allocation failures

#### 9.2.2 Diagnosis
1. Check content size vs. `IPFS_MAX_CONTENT_SIZE`
2. Monitor free heap memory
3. Verify content size from gateway

#### 9.2.3 Resolution
- Increase `IPFS_MAX_CONTENT_SIZE` (if memory allows)
- Split content into smaller chunks
- Use chunked download (future feature)

### 9.3 Publishing Failures

#### 9.3.1 Symptoms
- Publish operation fails
- No hash returned

#### 9.3.2 Diagnosis
1. Verify local IPFS node is running
2. Check IPFS node API accessibility
3. Verify API endpoint URL
4. Check IPFS node logs

#### 9.3.3 Resolution
- Start local IPFS node (`ipfs daemon`)
- Verify API is enabled
- Check firewall settings
- Verify API port (default 5001)

---

## 10.0 FUTURE ENHANCEMENTS

### 10.1 Planned Features
1. **Content Caching**: Cache frequently accessed content
2. **Chunked Downloads**: Support for larger files
3. **IPFS over Reticulum**: Direct IPFS protocol over Reticulum transport
4. **Pin Management**: Pin/unpin content via API

### 10.2 Advanced Features
1. **IPNS Support**: InterPlanetary Name System for mutable content
2. **IPLD Support**: Structured data linking
3. **PubSub**: IPFS pubsub over Reticulum
4. **Bitswap**: Direct peer-to-peer content exchange

---

## 11.0 REFERENCES

### 11.1 IPFS Documentation
- **IPFS Documentation**: https://docs.ipfs.io/
- **IPFS Gateway List**: https://ipfs.github.io/public-gateway-checker/
- **Content Addressing**: https://docs.ipfs.io/concepts/content-addressing/

### 11.2 API References
- **IPFS HTTP API**: https://docs.ipfs.io/reference/http/api/
- **Gateway API**: https://docs.ipfs.io/concepts/ipfs-gateway/

### 11.3 Additional Resources
- **Reticulum Network Stack**: https://reticulum.network/
- **IPFS Community**: https://discuss.ipfs.io/

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
