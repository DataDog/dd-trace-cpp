// Copyright 2021-Present Datadog, Inc. https://www.datadoghq.com/
// SPDX-License-Identifier: Apache-2.0

use libdd_library_config::{
    ConfigRead, ConfigReadError, Configurator, LibraryConfig, LibraryConfigSource, LoggedResult,
    ProcessInfo, MAX_CONFIG_FILE_SIZE,
};
use std::ffi::OsString;
use std::fs::File;
use std::io::Read;
use std::panic::{catch_unwind, AssertUnwindSafe};

struct FileReader;

impl ConfigRead for FileReader {
    type IoError = std::io::Error;

    fn read(&self, path: &str) -> Result<Vec<u8>, ConfigReadError<Self::IoError>> {
        let file = File::open(path).map_err(|error| match error.kind() {
            std::io::ErrorKind::NotFound => ConfigReadError::NotFound,
            _ => ConfigReadError::Io(error),
        })?;
        if file.metadata().map_err(ConfigReadError::Io)?.len() > MAX_CONFIG_FILE_SIZE as u64 {
            return Err(ConfigReadError::TooLarge);
        }
        let mut bytes = Vec::new();
        file.take((MAX_CONFIG_FILE_SIZE + 1) as u64)
            .read_to_end(&mut bytes)
            .map_err(ConfigReadError::Io)?;
        if bytes.len() > MAX_CONFIG_FILE_SIZE {
            return Err(ConfigReadError::TooLarge);
        }
        Ok(bytes)
    }
}

fn os_bytes(value: OsString) -> Vec<u8> {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStringExt;
        value.into_vec()
    }
    #[cfg(not(unix))]
    {
        value.to_string_lossy().as_bytes().to_vec()
    }
}

fn process_info(language: &str) -> ProcessInfo {
    let args = std::env::args_os().map(os_bytes).collect();
    let envp = std::env::vars_os()
        .map(|(key, value)| {
            let mut entry = os_bytes(key);
            entry.push(b'=');
            entry.extend(os_bytes(value));
            entry
        })
        .collect();
    ProcessInfo {
        args,
        envp,
        language: language.as_bytes().to_vec(),
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ByteSlice {
    data: *const u8,
    len: usize,
}

impl ByteSlice {
    fn from_str(value: &str) -> Self {
        Self {
            data: value.as_ptr(),
            len: value.len(),
        }
    }
}

#[repr(C)]
pub struct Entry {
    name: ByteSlice,
    value: ByteSlice,
    config_id: ByteSlice,
    source: u32,
}

pub struct ResultSet {
    configs: Vec<LibraryConfig>,
    error: Option<String>,
}

unsafe fn read_str<'a>(slice: ByteSlice) -> Result<&'a str, String> {
    if slice.data.is_null() {
        if slice.len == 0 {
            return Ok("");
        }
        return Err("null pointer with nonzero length".to_string());
    }
    let bytes = std::slice::from_raw_parts(slice.data, slice.len);
    std::str::from_utf8(bytes).map_err(|error| error.to_string())
}

fn load(
    language: ByteSlice,
    local_path: ByteSlice,
    fleet_path: ByteSlice,
) -> Result<Vec<LibraryConfig>, String> {
    let language = unsafe { read_str(language)? };
    let local_path = unsafe { read_str(local_path)? };
    let fleet_path = unsafe { read_str(fleet_path)? };
    let local_path = if local_path.is_empty() {
        Configurator::LOCAL_STABLE_CONFIGURATION_PATH
    } else {
        local_path
    };
    let fleet_path = if fleet_path.is_empty() {
        Configurator::FLEET_STABLE_CONFIGURATION_PATH
    } else {
        fleet_path
    };
    let process_info = process_info(language);
    match Configurator::new(false).get_config_from_reader(
        &FileReader,
        local_path,
        fleet_path,
        &process_info,
    ) {
        LoggedResult::Ok(configs, _) => Ok(configs),
        LoggedResult::Err(error) => Err(error.to_string()),
    }
}

#[no_mangle]
pub extern "C" fn dd_trace_stable_config_load(
    language: ByteSlice,
    local_path: ByteSlice,
    fleet_path: ByteSlice,
) -> *mut ResultSet {
    let outcome = catch_unwind(AssertUnwindSafe(|| load(language, local_path, fleet_path)));
    let result = match outcome {
        Ok(Ok(configs)) => ResultSet {
            configs,
            error: None,
        },
        Ok(Err(error)) => ResultSet {
            configs: Vec::new(),
            error: Some(error),
        },
        Err(_) => ResultSet {
            configs: Vec::new(),
            error: Some("stable config parser panicked".to_string()),
        },
    };
    Box::into_raw(Box::new(result))
}

#[no_mangle]
pub unsafe extern "C" fn dd_trace_stable_config_count(result: *const ResultSet) -> usize {
    result.as_ref().map_or(0, |result| result.configs.len())
}

#[no_mangle]
pub unsafe extern "C" fn dd_trace_stable_config_entry(
    result: *const ResultSet,
    index: usize,
    entry: *mut Entry,
) -> bool {
    let Some(entry) = entry.as_mut() else {
        return false;
    };
    let Some(config) = result.as_ref().and_then(|result| result.configs.get(index)) else {
        return false;
    };
    *entry = Entry {
        name: ByteSlice::from_str(&config.name),
        value: ByteSlice::from_str(&config.value),
        config_id: ByteSlice::from_str(config.config_id.as_deref().unwrap_or("")),
        source: match config.source {
            LibraryConfigSource::LocalStableConfig => 0,
            LibraryConfigSource::FleetStableConfig => 1,
        },
    };
    true
}

#[no_mangle]
pub unsafe extern "C" fn dd_trace_stable_config_error(result: *const ResultSet) -> ByteSlice {
    result
        .as_ref()
        .and_then(|result| result.error.as_deref())
        .map(ByteSlice::from_str)
        .unwrap_or(ByteSlice {
            data: std::ptr::null(),
            len: 0,
        })
}

#[no_mangle]
pub unsafe extern "C" fn dd_trace_stable_config_drop(result: *mut ResultSet) {
    if !result.is_null() {
        drop(Box::from_raw(result));
    }
}
