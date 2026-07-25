import { compressFile } from "./controllers/compression.controller"
import { downloadFile } from "./controllers/download.controller"
import { health } from "./controllers/health.controller"
import ApiError from "./utils/ApiError"
import { handleError } from "./utils/handleError"
import { withErrorHandler } from "./utils/withErrorHandler"

const server = Bun.serve({
  routes: {
    '/api/v1/health': {
      GET: withErrorHandler(health)
    },

    '/api/v1/compress': {
      POST: withErrorHandler(compressFile)
    },

    '/api/v1/download/:id/:filename': {
      GET: withErrorHandler(downloadFile)
    }
  },

  fetch: () => handleError(new ApiError(404, 'Resource not found'))
})

console.log(`Server is running at: ${server.url}`)
