// The UI-only probe has no map-download worker. Keep the shared renderer's
// invalidation hook linked without pretending that map downloads are present.
namespace orcsdr::map_download_notice { void invalidate() {} }
