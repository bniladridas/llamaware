# Llamaware

[![llamaware-agent](https://github.com/harpertoken/llamaware/actions/workflows/llamaware-agent.yml/badge.svg)](https://github.com/harpertoken/llamaware/actions/workflows/llamaware-agent.yml)

**Llamaware**  is a professional AI agent for development, built for **stability, extensibility, and security**. It provides core features for file operations, session handling, extensions, AI provider support, and automated GitHub repository management.

## System Requirements

**Supported Platforms**

* Linux (Ubuntu 20.04+)
* macOS (11.0+)
* Windows (MSVC 2019+)

**Compiler**

* C++17 compatible (GCC 9+, Clang 10+, MSVC 2019+)

**Build System**

* CMake 3.14+

**Version Control**

* Git

**Dependencies**

* cpr 1.10.0+
* nlohmann-json 3.10.0+
* OpenSSL 1.1.1+

## Project Structure

* **src/core/** – central agent logic, command routing
* **src/services/** – AI, file, web, git, MCP, and more
* **src/utils/** – configuration, UI, validation helpers
* **tests/** – unit and E2E tests
* **scripts/** – build & report utilities
* **docker/** – Docker and containerization files

## Installation & Build

### Common Steps

```bash
git clone https://github.com/harpertoken/llamaware.git
cd llamaware
```

### Install Dependencies

**Ubuntu/Debian**

```bash
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev
make install-deps-ubuntu
```

**macOS**

```bash
brew install cmake
```

**Windows**

```powershell
# Install Chocolatey, CMake, and vcpkg (see CI workflow)
```

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -- -j$(nproc)
```

### Optional: Generate Progress Report

```bash
./scripts/report.sh
```

## Running the Agent

```bash
./build/bin/llamaware-agent
```

**Basic Commands**

```text
version
search:query
cmd:command
read:/path/to/file
write:/path/to/file content
help
exit | quit
```

**Example Session**

```bash
docker exec -it llamaware-agent /bin/bash
llamaware-agent

# Inside agent
version
cmd:ls -la
search:current time
exit
```

**Persistent Data**

* Stored in Docker volumes

```bash
docker volume ls
```

**Environment Variables**

* `.env` file in project root for API keys

## Supported AI Providers

* **Together AI** – Meta Llama models
* **Cerebras** – Llama inference
* **Fireworks** – High-performance models
* **Groq** – Speed optimized
* **DeepSeek** – Advanced reasoning models
* **OpenAI** – GPT models
* **Offline** – Ollama (llama3.2:3b, latest)

## GitHub Bot

Llamaware includes automated repository management features.

**Core Features**

* Health checks (build/tests/quality)
* TODO detection and sub-issue creation
* Milestone assignment based on PR type

**Configuration**

* `.llamaware-agent.yml` in repository root

```yaml
name: llamaware-agent
version: 1.0.0
features:
  - health_checks
  - todo_management
  - milestone_assignment
permissions:
  contents: read
  pull-requests: write
  issues: write
```

**Workflow**

* Triggers on PRs, issues, and comments
* Posts formatted progress reports on PRs

## CI/CD

The project uses GitHub Actions for continuous integration and deployment.

**Workflow Triggers**
- Push to `main` and `develop` branches
- Pull requests to any branch
- Published releases

**Repository Variables**
- `PUSH_BRANCHES`: JSON array of branches for push triggers (e.g., `["main","develop"]`)
- `PR_BRANCHES`: JSON array of branches for PR triggers (e.g., `["*"]`)
- `RELEASE_TYPES`: JSON array of release types (e.g., `["published"]`)

**Note**: Due to GitHub Actions limitations, trigger branches are hardcoded in the workflow. Repository variables are used for job configuration. For dynamic triggers, monitor GitHub updates.

**Features**
- Cross-platform builds (Ubuntu, macOS, Windows)
- Preflight checks
- Artifact uploads
- Code coverage reporting

## Testing

**Unit Tests**

```bash
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

**End-to-End (E2E) Tests**

```bash
docker-compose -f docker-compose.e2e.yml up -d
docker-compose -f docker-compose.e2e.yml logs -f e2e-tests
docker-compose -f docker-compose.e2e.yml down
```

## Development

**Debug Build**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

**Workflow**

* Use `help` inside the agent to explore commands
* Follow CI/CD guides for contributing

## Troubleshooting

```bash
# Verify container is running
docker ps

# View logs
docker logs llamaware-agent

# Restart agent
docker-compose -f package/docker/docker-compose.yml restart llamaware-agent
```

## License

MIT License
