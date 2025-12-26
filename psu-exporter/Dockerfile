FROM python:3.9-slim

WORKDIR /app

RUN pip install pyserial prometheus_client

COPY exporter.py .

CMD ["python", "-u", "exporter.py"]
