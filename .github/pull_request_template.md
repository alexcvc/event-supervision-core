## Summary

<!-- What does this change do, and why? -->

## Related issue

<!-- Closes #... , if applicable -->

## Changes

- 

## Testing

- [ ] `ctest --test-dir build --output-on-failure` passes locally
- [ ] Added/updated tests covering this change
- [ ] Ran `clang-format` / `clang-tidy` (or confirmed CI will)

## Checklist

- [ ] New timing/`EventMode` behavior (if any) lives inside `EventDescriptor`'s existing Debounce/Heartbeat phase machine, not a parallel mechanism
- [ ] No `.cpp` files added to the library itself (header-only)
- [ ] Documentation (`doc/developer-guide.md`, `README.md`, `AGENTS.md`) updated if architecture or usage changed