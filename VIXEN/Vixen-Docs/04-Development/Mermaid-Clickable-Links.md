---
title: Mermaid Clickable Links in Obsidian
aliases: [Mermaid Links, Diagram Navigation, Clickable Diagrams]
tags: [mermaid, obsidian, navigation, documentation]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[../00-Index/Home]]"
---

# Mermaid Clickable Links in Obsidian

This guide covers how to make Mermaid diagram nodes clickable for navigation within an Obsidian vault.

---

## 1. Quick Reference

| Method | Syntax | Use Case |
|--------|--------|----------|
| `internal-link` class | `class NodeId internal-link;` | Link to note matching node label |
| `obsidian://` URI | `click NodeId "obsidian://open?vault=X&file=Y"` | Link to specific file |
| `flowchart` + URL | `click NodeId "URL"` | External links |

---

## 2. Method 1: internal-link Class (Recommended)

The simplest approach for internal vault navigation. Available since Obsidian v0.9.21.

### 2.1 Basic Syntax

```mermaid
flowchart LR
    A[RenderGraph] --> B[SVO]
    A --> C[VulkanResources]
    
    class A internal-link
    class B internal-link
    class C internal-link
```

**How it works:**
- The node label becomes the link target
- `A[RenderGraph]` links to a note named "RenderGraph"
- Supports hover preview (Ctrl+hover)
- Works with notes in folders (not vault root)

### 2.2 Multiple Nodes

```mermaid
flowchart TD
    Core[Core] --> Logger[Logger]
    Logger --> EventBus[EventBus]
    Core --> VulkanResources[VulkanResources]
    
    class Core,Logger,EventBus,VulkanResources internal-link
```

### 2.3 Limitations

- Node label MUST match the target note name exactly
- Notes in vault root may not work - use folders
- Cannot display different text than the link target

---

## 3. Method 2: Obsidian URI (Full Control)

Use `obsidian://` URLs for precise control over link targets.

### 3.1 Basic Syntax

```mermaid
flowchart LR
    A[Architecture] --> B[Implementation]
    
    click A "obsidian://open?vault=Vixen-Docs&file=01-Architecture/Overview"
    click B "obsidian://open?vault=Vixen-Docs&file=02-Implementation/Overview"
```

### 3.2 URI Format

```
obsidian://open?vault=VaultName&file=FolderPath/FileName
```

| Parameter | Description | Example |
|-----------|-------------|---------|
| `vault` | Vault name (spaces: use `%20`) | `Vixen-Docs` |
| `file` | Path relative to vault root | `Libraries/SVO` |

### 3.3 With Tooltips

```mermaid
flowchart TD
    SVO[SVO Library] --> Core[Core]
    
    click SVO "obsidian://open?vault=Vixen-Docs&file=Libraries/SVO" "Open SVO documentation"
    click Core "obsidian://open?vault=Vixen-Docs&file=Libraries/Core" "Open Core documentation"
```

---

## 4. Critical: Use flowchart, Not graph

**IMPORTANT**: Click events only work reliably with `flowchart`, not `graph`.

### 4.1 Working Example

```mermaid
flowchart LR
    A[Home] --> B[Libraries]
    click A "obsidian://open?vault=Vixen-Docs&file=00-Index/Home"
```

### 4.2 Non-Working Example

```mermaid
flowchart LR
    A[Home] --> B[Libraries]
    click A "obsidian://open?vault=Vixen-Docs&file=00-Index/Home"
```

The `graph` syntax renders different HTML that does not support click callbacks in Obsidian.

---

## 5. Combining Methods

You can mix `internal-link` class with explicit `click` directives:

```mermaid
flowchart TD
    subgraph Navigation
        Home[Home]
        Arch[Architecture]
        Impl[Implementation]
    end
    
    Home --> Arch
    Home --> Impl
    Arch --> RG[RenderGraph System]
    Impl --> SVO[SVO System]
    
    %% internal-link for simple cases
    class Home,Arch,Impl internal-link
    
    %% Explicit URIs for nested paths
    click RG "obsidian://open?vault=Vixen-Docs&file=01-Architecture/RenderGraph-System"
    click SVO "obsidian://open?vault=Vixen-Docs&file=02-Implementation/SVO-System"
```

---

## 6. VIXEN Vault Examples

### 6.1 Library Dependency Graph (Clickable)

```mermaid
flowchart TD
    subgraph Foundation
        C[Core]
        L[Logger]
    end
    
    subgraph Vulkan
        VR[VulkanResources]
        CS[CashSystem]
    end
    
    subgraph Rendering
        RG[RenderGraph]
    end
    
    subgraph Voxel
        SVO[SVO]
        VD[VoxelData]
    end
    
    C --> L
    L --> VR
    VR --> CS
    CS --> RG
    RG --> SVO
    VD --> SVO
    
    class C internal-link
    class L internal-link
    class VR internal-link
    class CS internal-link
    class RG internal-link
    class SVO internal-link
    class VD internal-link

    style RG fill:#4a9eff
    style SVO fill:#26de81
```

### 6.2 Section Navigation (Clickable)

```mermaid
flowchart LR
    A[Home] --> B[Architecture]
    A --> C[Implementation]
    A --> D[Research]
    A --> E[Development]
    A --> F[Libraries]
    
    click A "obsidian://open?vault=Vixen-Docs&file=00-Index/Home"
    click B "obsidian://open?vault=Vixen-Docs&file=01-Architecture/Overview"
    click C "obsidian://open?vault=Vixen-Docs&file=02-Implementation/Overview"
    click D "obsidian://open?vault=Vixen-Docs&file=03-Research/Overview"
    click E "obsidian://open?vault=Vixen-Docs&file=04-Development/Overview"
    click F "obsidian://open?vault=Vixen-Docs&file=Libraries/Overview"
    
    style A fill:#4a9eff
```

---

## 7. Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| Click does nothing | Using `graph` instead of `flowchart` | Change to `flowchart` |
| Link opens wrong file | Node label doesn't match file name | Use explicit `click` with URI |
| Links don't work in root | Obsidian limitation | Move notes to folders |
| Hover preview missing | Not using `internal-link` class | Add `class NodeId internal-link` |
| Spaces in file name | URI encoding | Use `%20` for spaces |

---

## 8. Best Practices

1. **Always use `flowchart`** for clickable diagrams
2. **Prefer `internal-link` class** for simple same-name links
3. **Use `obsidian://` URIs** when display text differs from file name
4. **Add tooltips** for complex diagrams: `click A "URL" "Description"`
5. **Test links** after creating - Obsidian version differences may affect behavior

---

## 9. References

- [Obsidian Forum: Obsidian Links in Mermaid](https://forum.obsidian.md/t/obsidian-links-in-mermaid/2965)
- [Obsidian Forum: Internal Links in Mermaid](https://forum.obsidian.md/t/internal-links-in-mermaid/9562)
- [Obsidian Forum: Flowchart vs Graph URL Behavior](https://forum.obsidian.md/t/mermaid-url-opens-in-flowchart-but-not-in-graph/22088)
- [Mermaid Official Documentation](https://mermaid.js.org/syntax/flowchart.html#interaction)

---

## 10. Related Pages

- [[Overview|Development Overview]] - Development documentation
- [[../00-Index/Home|Home]] - Vault home page
- [[../Libraries/Overview|Libraries]] - Library documentation

