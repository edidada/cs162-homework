//! The gRPC server.
//!

use crate::{SERVER_ADDR, log, rpc::kv_store::*};
use anyhow::Result;
use std::{collections::HashMap, sync::Arc};
use tokio::sync::RwLock;
use tonic::{Request, Response, Status, transport::Server};

#[derive(Default)]
pub struct KvStore {
    store: Arc<RwLock<HashMap<Vec<u8>, Vec<u8>>>>,
}

#[tonic::async_trait]
impl kv_store_server::KvStore for KvStore {
    async fn example(
        &self,
        req: Request<ExampleRequest>,
    ) -> Result<Response<ExampleReply>, Status> {
        log::info!("Received example request.");
        Ok(Response::new(ExampleReply {
            output: req.into_inner().input + 1,
        }))
    }

    async fn echo(&self, req: Request<EchoRequest>) -> Result<Response<EchoReply>, Status> {
        Ok(Response::new(EchoReply { msg: req.into_inner().msg }))
    }
    async fn put(&self, req: Request<PutRequest>) -> Result<Response<PutReply>, Status> {
        let req = req.into_inner();
        self.store.write().await.insert(req.key, req.value);
        Ok(Response::new(PutReply {}))
    }
    async fn get(&self, req: Request<GetRequest>) -> Result<Response<GetReply>, Status> {
        let value = self.store.read().await.get(&req.into_inner().key).cloned()
            .ok_or_else(|| Status::not_found("Key does not exist."))?;
        Ok(Response::new(GetReply { value }))
    }
}

pub async fn start() -> Result<()> {
    let svc = kv_store_server::KvStoreServer::new(KvStore::default());

    log::info!("Starting KV store server.");
    Server::builder()
        .add_service(svc)
        .serve(SERVER_ADDR.parse().unwrap())
        .await?;
    Ok(())
}
