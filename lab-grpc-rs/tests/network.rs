use lab_grpc::client::{echo, get, put};
use anyhow::Result;
#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn concurrent_network() -> Result<()> {
    let mut clients = Vec::new();
    for id in 0..16u8 {
        clients.push(tokio::spawn(async move {
            let key = vec![id, 0, 255];
            for round in 0..20u8 {
                let value = vec![id, 0, round, 255];
                put(key.clone(), value.clone()).await?;
                assert_eq!(get(key.clone()).await?, value);
            }
            anyhow::Ok(())
        }));
    }
    for client in clients { client.await??; }
    for size in [0, 1, 4096, 65536, 1048576] {
        let data: Vec<u8> = (0..size).map(|i| (i % 256) as u8).collect();
        put(Vec::new(), data.clone()).await?;
        assert_eq!(get(Vec::new()).await?, data);
    }
    let msg = "中文🦀\0echo".to_string();
    assert_eq!(echo(msg.clone()).await?, msg);
    let error = get(b"missing-concurrent-network".to_vec()).await.unwrap_err();
    let status = error.downcast_ref::<tonic::Status>().unwrap();
    assert_eq!(status.code(), tonic::Code::NotFound);
    assert_eq!(status.message(), "Key does not exist.");
    println!("16 clients x20 overwrite/read cycles; five sizes through 1MiB; Unicode/NUL and NotFound passed");
    Ok(())
}
