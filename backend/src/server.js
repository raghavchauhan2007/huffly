import { compressFile } from "./controllers/compression.controller"
import { health } from "./controllers/health.controller"
import ApiError from "./utils/ApiError"

const server = Bun.serve({
  routes: {
    '/api/v1/health': {
      GET: health
    },

    '/api/v1/compress': {
      POST: compressFile
    }
  },

  fetch: () => Response.json(new ApiError(404, 'Resource not found'))
})

console.log(`Server is running at: ${server.url}`)
