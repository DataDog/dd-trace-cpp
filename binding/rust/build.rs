fn main() {
    println!("cargo:rustc-link-lib=dylib=dd_trace_c");
    println!("cargo:rustc-link-search=native=../../.build/binding/c");

}