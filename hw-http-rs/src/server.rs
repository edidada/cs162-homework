use std::{env, sync::Arc};
use tokio::{fs, io::{AsyncReadExt, AsyncWriteExt}, sync::RwLock};
lazy_static::lazy_static! { static ref STATS: StatsPtr = Arc::new(RwLock::new(Stats::new())); }
use std::net::{Ipv4Addr, SocketAddrV4};

use crate::args;

use crate::http::*;
use crate::stats::*;

use clap::Parser;
use tokio::net::{TcpListener, TcpStream};

use anyhow::Result;

pub fn main() -> Result<()> {
    // Configure logging
    // You can print logs (to stderr) using
    // `log::info!`, `log::warn!`, `log::error!`, etc.
    env_logger::Builder::new()
        .filter_level(log::LevelFilter::Info)
        .init();

    // Parse command line arguments
    let args = args::Args::parse();

    // Set the current working directory
    env::set_current_dir(&args.files)?;

    // Print some info for debugging
    log::info!("HTTP server initializing ---------");
    log::info!("Port:\t\t{}", args.port);
    log::info!("Num threads:\t{}", args.num_threads);
    log::info!("Directory:\t\t{}", &args.files);
    log::info!("----------------------------------");

    // Initialize a thread pool that starts running `listen`
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .worker_threads(args.num_threads)
        .build()?
        .block_on(listen(args.port))
}

async fn listen(port: u16) -> Result<()> {
    // Hint: you should call `handle_socket` in this function.
    let listener = TcpListener::bind(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, port)).await?;
    loop {
        match listener.accept().await {
            Ok((socket, _)) => { tokio::spawn(async move {
                if let Err(error) = handle_socket(socket).await { log::warn!("{}", error); }
            }); }
            Err(error) => log::warn!("accept: {}", error),
        }
    }
}

// Handles a single connection via `socket`.
async fn handle_socket(mut socket: TcpStream) -> Result<()> {
    let request = match parse_request(&mut socket).await {
        Ok(request) => request,
        Err(_) => { respond(&mut socket, 400, "text/plain", Some(0)).await?; return Ok(()); }
    };
    if request.path == "/stats" {
        let snapshot = STATS.read().await.items();
        start_response(&mut socket, 200).await?;
        send_header(&mut socket, "Content-Type", "text/plain").await?;
        end_headers(&mut socket).await?;
        for (code, count) in snapshot {
            socket.write_all(format!("{}: {}\n", response_message(code), count).as_bytes()).await?;
        }
        return Ok(());
    }
    if !request.path.starts_with('/') || request.path.split('/').any(|part| part == "..") {
        respond(&mut socket, 403, "text/plain", Some(0)).await?; return Ok(());
    }
    if request.method != "GET" {
        respond(&mut socket, 405, "text/plain", Some(0)).await?; return Ok(());
    }
    let path = format!(".{}", request.path);
    match fs::metadata(&path).await {
        Ok(meta) if meta.is_file() => serve_file(&mut socket, &path).await?,
        Ok(meta) if meta.is_dir() => {
            let index = format_index(&path);
            if fs::metadata(&index).await.map(|m| m.is_file()).unwrap_or(false) {
                serve_file(&mut socket, &index).await?;
            } else {
                let mut entries = fs::read_dir(&path).await?;
                respond(&mut socket, 200, "text/html", None).await?;
                let parent = std::path::Path::new(&request.path).parent()
                    .and_then(|p| p.to_str()).unwrap_or("/").trim_start_matches('/');
                socket.write_all(format_href(parent, "..").as_bytes()).await?;
                while let Some(entry) = entries.next_entry().await? {
                    let name = entry.file_name().to_string_lossy().into_owned();
                    let url = format!("{}/{}", request.path.trim_matches('/'), name);
                    socket.write_all(format_href(&url, &name).as_bytes()).await?;
                }
            }
        }
        _ => respond(&mut socket, 404, "text/plain", Some(0)).await?,
    }
    Ok(())
}

// You are free (and encouraged) to add other funtions to this file.
// You can also create your own modules as you see fit.

async fn respond(socket: &mut TcpStream, code: StatusCode, mime: &str, length: Option<u64>) -> Result<()> {
    incr(&STATS, code).await;
    start_response(socket, code).await?;
    send_header(socket, "Content-Type", mime).await?;
    if let Some(length) = length { send_header(socket, "Content-Length", &length.to_string()).await?; }
    end_headers(socket).await
}
async fn serve_file(socket: &mut TcpStream, path: &str) -> Result<()> {
    let mut file = fs::File::open(path).await?;
    let length = file.metadata().await?.len();
    respond(socket, 200, get_mime_type(path), Some(length)).await?;
    let mut buffer = [0u8; 1024];
    loop {
        let n = file.read(&mut buffer).await?;
        if n == 0 { break; }
        socket.write_all(&buffer[..n]).await?;
    }
    Ok(())
}
