variable "REPO" {
  default = "lifflander1/vt"
}

variable "GIT_BRANCH" {}

function "arch" {
  params = [item]
  result = lookup(item, "arch", "amd64")
}

function "variant" {
  params = [item]
  result = lookup(item, "variant", "")
}

function "target_suffix" {
  params = [item]
  result = variant(item) == "" ? "" : "-${variant(item)}"
}

target "lb-build" {
  target = "build"
  context = "."
  dockerfile = "ci/docker/lb.dockerfile"

  platforms = [
    "linux/amd64",
    # "linux/arm64"
  ]
  ulimits = [
    "core=0"
  ]

  secret = ["id=GITHUB_TOKEN,env=GITHUB_TOKEN"]
}

target "lb-build-all" {
  name = "lb-build-${replace(item.image, ".", "-")}${target_suffix(item)}"
  inherits = ["lb-build"]
  tags = ["${REPO}:lb-${item.image}"]

  args = {
    ARCH = arch(item)
    GIT_BRANCH = "${GIT_BRANCH}"
    IMAGE = "wf-${item.image}"
    REPO = REPO
  }

  # to get the list of available images from DARMA-tasking/workflows:
  # workflows > docker buildx bake --print build-all | grep "lifflander1/vt:"
  matrix = {
    item = [
      {
        image = "amd64-alpine-3.16-clang-cpp"
      },
      {
        image = "amd64-ubuntu-20.04-clang-10-cpp"
      },
      {
        image = "amd64-ubuntu-20.04-clang-9-cpp"
      },
      {
        image = "amd64-ubuntu-20.04-gcc-10-cpp"
      },
      {
        image = "amd64-ubuntu-20.04-gcc-10-openmpi-cpp"
      },
      {
        image = "amd64-ubuntu-20.04-gcc-9-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-clang-11-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-clang-12-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-clang-13-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-clang-14-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-clang-15-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-gcc-11-cpp"
      },
      {
        image = "amd64-ubuntu-22.04-gcc-12-cpp"
      },
      {
        image = "amd64-ubuntu-24.04-clang-17-cpp"
      },
      {
        image = "amd64-ubuntu-24.04-clang-18-cpp"
      },
      {
        image = "amd64-ubuntu-24.04-gcc-13-cpp"
      },
      {
        image = "amd64-ubuntu-24.04-gcc-14-cpp"
      }
    ]
  }
}
