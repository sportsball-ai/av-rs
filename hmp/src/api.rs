use serde::{Deserialize, Deserializer};

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LoginRequestBody<'a> {
    pub username: &'a str,
    pub password: &'a str,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Section {
    pub name: String,
    pub id: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct SectionsResponseBody {
    pub data: Vec<Section>,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Source {
    pub name: String,
    pub id: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Asset {
    pub title: String,
    pub id: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
#[serde(tag = "itemType")]
pub enum Content {
    Source(Source),
    Asset(Asset),
    #[serde(other)]
    Other,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct SectionContentResponseBody {
    pub data: Vec<Content>,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
#[serde(default, rename_all = "camelCase")]
pub struct SRTEndpointDetails {
    #[serde(rename = "srtMode")]
    pub mode: String,

    #[serde(rename = "srtEncryption")]
    pub encryption: String,

    #[serde(rename = "srtPassPhrase")]
    pub passphrase: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase", tag = "type")]
pub enum EndpointDetails {
    #[serde(deserialize_with = "deserialize_srt_details", rename = "srt")]
    SRT(SRTEndpointDetails),
    #[serde(other)]
    None,
}

fn deserialize_srt_details<'de, D: Deserializer<'de>>(deserializer: D) -> Result<SRTEndpointDetails, D::Error> {
    #[derive(Deserialize)]
    struct SRTEndpointDetailsWrapper {
        srt: SRTEndpointDetails,
    }
    SRTEndpointDetailsWrapper::deserialize(deserializer).map(|w| w.srt)
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Endpoint {
    pub uri: String,
    #[serde(flatten)]
    pub details: EndpointDetails,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct PlayerEndpointResponseBody {
    pub data: Vec<Endpoint>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deserialization() {
        assert_eq!(
            SectionsResponseBody {
                data: vec![Section {
                    name: "Foo".to_string(),
                    id: "theid".to_string(),
                }],
            },
            serde_json::from_str(
                r##"{
                    "data": [{
                            "index": 1,
                            "name": "Foo",
                            "id": "theid"
                    }],
                    "paging": {
                            "results": 1,
                            "pageSize": 100
                    }
                }"##
            )
            .unwrap(),
        );

        assert_eq!(
            SectionContentResponseBody {
                data: vec![
                    Content::Other,
                    Content::Source(Source {
                        name: "Foo".to_string(),
                        id: "theid".to_string(),
                    })
                ],
            },
            serde_json::from_str(
                r##"{
                    "data": [{
                        "itemType": "notsource"
                    }, {
                        "description": "",
                        "ctime": 1597426519,
                        "mtime": 1597426519,
                        "host": "1.2.3.4",
                        "port": 51011,
                        "type": "SRT",
                        "continuous": true,
                        "mode": "CALLER",
                        "latency": 20,
                        "passphrase": "",
                        "gatewayId": "gatewayid",
                        "iptv": true,
                        "iptvIndex": 77,
                        "deviceProfile": "Low Latency",
                        "id": "theid",
                        "name": "Foo",
                        "bitrate": 0,
                        "active": false,
                        "creator": {
                                "username": "haiadmin"
                        },
                        "itemType": "source",
                        "stitle": "Foo Bar",
                        "sections": [{
                                "level": "base",
                                "publishedTime": 1597427366,
                                "id": "sectionid"
                        }]
                    }],
                    "paging": {
                            "results": 30,
                            "pageSize": 50
                    }
                }"##
            )
            .unwrap(),
        );

        assert_eq!(
            PlayerEndpointResponseBody {
                data: vec![
                    Endpoint {
                        uri: "srt://1.2.3.4:51029".to_string(),
                        details: EndpointDetails::SRT(SRTEndpointDetails {
                            mode: "caller".to_string(),
                            encryption: "AES256".to_string(),
                            passphrase: "passphrase".to_string(),
                        }),
                    },
                    Endpoint {
                        uri: "https://1.2.3.4:443/calypso_proxy/foo.m3u8".to_string(),
                        details: EndpointDetails::None,
                    },
                ],
            },
            serde_json::from_str(
                r##"{
                        "data": [{
                                "type": "srt",
                                "uri": "srt://1.2.3.4:51029",
                                "srt": {
                                        "srtMode": "caller",
                                        "srtEncryption": "AES256",
                                        "srtPassPhrase": "passphrase",
                                        "tos": 104,
                                        "srtLatency": 150
                                }
                        }, {
                                "type": "hls",
                                "uri": "https://1.2.3.4:443/calypso_proxy/foo.m3u8"
                        }]
                }"##
            )
            .unwrap(),
        );
    }
}
