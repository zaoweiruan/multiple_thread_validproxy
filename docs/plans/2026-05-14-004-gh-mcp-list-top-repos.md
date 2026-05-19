---
title: "plan(gh-mcp): List top 5 GitHub repos by stars (read-only)"
type: plan
status: draft
date: 2026-05-14
origin: ".kilo/plans/1776215451920-nimble-wolf.md"
---

# Plan: List top 5 GitHub repositories by stars using GitHub MCP (READ-ONLY)

## Objective
Identify the five GitHub repositories with the highest star counts, excluding forks, using GitHub MCP (GitHub API wrapper) in a read-only flow.

## Assumptions & Constraints
- Only read operations are performed; no repository modifications.
- Exclude forks to reflect original projects.
- Return 5 repositories with essential fields: full_name, html_url, description, stargazers_count.
- Order is descending by star count (highest first).

## Data Source
- GitHub search API via MCP: query filters for stars and non-forks.

## Approach

1. Query construction: search for repositories with a positive star count and fork:false.
2. Use MCP wrapper to fetch top results: page=1, per_page=5, query accordingly.
3. Normalize results into a consistent schema for display.
4. If API returns forks or missing data, apply fallback logic and re-query if needed.

## MCP Tool Invocation (Plan)
- Tool: github_search_repositories
- Parameters: {
  query: "stars:>0 fork:false",
  page: 1,
  perPage: 5
}

Note: The MCP tool signature only exposes query, page, perPage. Sorting by stars is assumed to be the default order for search results or implemented server-side.

## Output Format
- A list of 5 repositories with the following fields:
  - full_name (e.g., owner/name)
  - html_url
  - description
  - stargazers_count
- Present in descending order by stars.

## Validation & Acceptance Criteria
- Exactly 5 repositories returned (or fewer if API constraints apply) and none are forks.
- Each entry contains non-empty full_name and html_url.

## Risks & Mitigations
- Potential API latency or rate limits: rely on MCP caching and provide informative fallback messages.
- In case of ambiguous results, document the discrepancy and propose re-query with adjusted filters (e.g., different star thresholds).

## Next Steps
- If results look good, present the list in a human-friendly format and export to a simple Markdown snippet for reuse.
- Optionally run a second pass to surface top non-fork languages or topics.

## Plan Exit
Proceed to implementation by executing the MCP read-only query as described above, then report the 5 results.
