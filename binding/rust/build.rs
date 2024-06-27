fn main() {
    println!("cargo:rustc-link-lib=dylib=dd_trace_c");
    println!("cargo:rustc-link-search=native=../../.build/binding/c");

    built::write_built_file().expect("Failed to acquire build-time information");
}