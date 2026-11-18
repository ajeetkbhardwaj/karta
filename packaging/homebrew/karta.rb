class Karta < Formula
  desc "Terminal-first coding agent with an ncurses TUI and CLI"
  homepage "https://github.com/ajeetkbhardwaj/karta"
  url "https://github.com/ajeetkbhardwaj/karta/archive/refs/tags/v0.1.0.tar.gz"
  # Replace after the v0.1.0 tag is published:
  #   curl -fsSL https://github.com/ajeetkbhardwaj/karta/archive/refs/tags/v0.1.0.tar.gz | shasum -a 256
  sha256 "REPLACE_WITH_SOURCE_TARBALL_SHA256"
  license "MIT"
  head "https://github.com/ajeetkbhardwaj/karta.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ncurses"
  depends_on "openssl@3"

  def install
    system "cmake", "-S", ".", "-B", "build",
           *std_cmake_args,
           "-DKARTA_BUILD_TUI=ON",
           "-DKARTA_BUILD_TESTS=OFF",
           "-DOPENSSL_ROOT_DIR=#{Formula["openssl@3"].opt_prefix}"
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/karta --version")
  end
end
