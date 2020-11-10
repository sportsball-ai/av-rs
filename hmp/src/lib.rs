#[macro_use]
extern crate serde_derive;

use percent_encoding::{utf8_percent_encode, AsciiSet, NON_ALPHANUMERIC};

mod api;
pub use api::{Content, Endpoint, EndpointDetails, SRTEndpointDetails, Section, Source};

pub type Error = reqwest::Error;
pub type Result<T> = std::result::Result<T, Error>;

const QUERY_ESCAPE_SET: &AsciiSet = &NON_ALPHANUMERIC.remove(b'-');

pub struct Client {
    client: reqwest::Client,
    url: String,
}

impl Client {
    pub fn new<S: AsRef<str>>(url: S) -> Result<Self> {
        Ok(Self {
            client: reqwest::Client::builder().cookie_store(true).build()?,
            url: url.as_ref().trim_end_matches('/').to_string(),
        })
    }

    pub fn authentication(&mut self) -> ClientAuthentication<'_> {
        ClientAuthentication { client: self }
    }

    pub async fn sections(&self, page_size: i32, page: i32) -> Result<Vec<Section>> {
        let url = format!("{}/apis/_/sections?pageSize={}&page={}", &self.url, page_size, page);
        let resp = self.client.get(&url).send().await?;
        resp.error_for_status_ref()?;
        let resp = resp.json::<api::SectionsResponseBody>().await?;
        Ok(resp.data)
    }

    pub fn section<'a>(&'a self, id: &'a str) -> ClientSection<'a> {
        ClientSection { client: self, id }
    }

    pub fn player(&mut self) -> ClientPlayer<'_> {
        ClientPlayer { client: self }
    }
}

pub struct ClientAuthentication<'a> {
    client: &'a mut Client,
}

impl<'a> ClientAuthentication<'a> {
    pub async fn login(&self, username: &str, password: &str) -> Result<()> {
        let url = format!("{}/apis/authentication/login", &self.client.url);
        let resp = self.client.client.post(&url).json(&api::LoginRequestBody { username, password }).send().await?;
        resp.error_for_status_ref()?;
        Ok(())
    }
}

pub struct ClientSection<'a> {
    client: &'a Client,
    id: &'a str,
}

impl<'a> ClientSection<'a> {
    pub async fn content(&self, page_size: i32, page: i32) -> Result<Vec<Content>> {
        let url = format!(
            "{}/apis/_/sections/{}/content?pageSize={}&page={}",
            &self.client.url,
            utf8_percent_encode(&self.id, QUERY_ESCAPE_SET),
            page_size,
            page
        );
        let resp = self.client.client.get(&url).send().await?;
        resp.error_for_status_ref()?;
        let resp = resp.json::<api::SectionContentResponseBody>().await?;
        Ok(resp.data)
    }
}

pub struct ClientPlayer<'a> {
    client: &'a Client,
}

pub enum ContentId<'a> {
    Source(&'a str),
    Asset(&'a str),
}

impl<'a> ClientPlayer<'a> {
    pub async fn endpoint(&self, content_id: ContentId<'_>) -> Result<Vec<Endpoint>> {
        let url = format!(
            "{}/apis/_/player/endpoint?acceptableEndpointTypes=srt&contentId={}&contentType={}",
            &self.client.url,
            utf8_percent_encode(
                match content_id {
                    ContentId::Source(s) => s,
                    ContentId::Asset(s) => s,
                },
                QUERY_ESCAPE_SET
            ),
            match content_id {
                ContentId::Source(_) => "source",
                ContentId::Asset(_) => "asset",
            },
        );
        let resp = self.client.client.get(&url).send().await?;
        resp.error_for_status_ref()?;
        let resp = resp.json::<api::PlayerEndpointResponseBody>().await?;
        Ok(resp.data)
    }
}
