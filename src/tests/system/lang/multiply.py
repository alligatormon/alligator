import json

def alligator_run(text, metrics, conf):
    config_res = { "entrypoint": [
        {
          "tcp": [
            "1112"
          ]
        }
      ],
    }
    return json.dumps(config_res)
