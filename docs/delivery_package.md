# Delivery Package

## Positioning
- Product shape: source package + Docker deployment + acceptance scripts
- Target customer: small teams and studios running in private networks or private cloud
- Non-goals: SaaS hosting, multi-tenant control plane, production-grade public Internet operations

## Package Contents
- Backend source code
- Demo and delivery compose files
- MySQL bootstrap scripts
- `.env.example` and delivery env template
- `bootstrap/up/smoke/verify/down` scripts
- Password hash helper and readiness checker

## Deployment Profiles
- `demo`: exposes MySQL and Redis to the host for local verification and portfolio demos
- `delivery`: only exposes the Nginx ingress port and keeps stateful services inside the compose network

## Acceptance Entry Points
- `./scripts/bootstrap.sh`
- `./scripts/up.sh`
- `./scripts/smoke.sh`
- `./scripts/verify.sh`
- `./scripts/down.sh`
