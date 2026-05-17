from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path
from typing import Any, Iterable, Mapping

def _default_nats_url() -> str:
    return os.environ.get("NATS_URL") or os.environ.get("NATSURL") or "nats://localhost:4222"


def _default_base_subject() -> str:
    return os.environ.get("SUBJECT", "detersl.worker")


def _coerce_request_id(value: Any) -> int | None:
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.isdigit():
        return int(value)
    return None


def _load_nats():
    try:
        import nats as nats_module
        from nats.errors import NoRespondersError, TimeoutError
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "detersl_client requires the `nats-py` package at runtime"
        ) from exc
    return nats_module, NoRespondersError, TimeoutError


class AsyncDeterSLClient:
    def __init__(
        self,
        url: str | None = None,
        *,
        base_subject: str | None = None,
        core_subject: str | None = None,
        invoke_subject: str | None = None,
        metrics_subject: str | None = None,
        metrics_durable: str = "detersl-status",
        publish_async_max_pending: int = 50000,
        registration_timeout_s: float | None = None,
        registration_max_retries: int | None = None,
        registration_retry_backoff_s: float | None = None,
        metrics_pull_batch: int = 1000,
        metrics_pull_timeout_s: float = 0.1,
        metrics_wait_timeout_s: float = 120.0,
    ) -> None:
        base = base_subject or _default_base_subject()
        self.url = url or _default_nats_url()
        self.core_subject = core_subject or os.environ.get("CORE_SUBJECT") or f"{base}.core"
        self.invoke_subject = invoke_subject or f"{base}.invoke"
        self.metrics_subject = (
            metrics_subject
            or os.environ.get("STATUS_SUBJECT")
            or os.environ.get("METRICS_SUBJECT")
            or f"{base}.status"
        )
        self.metrics_durable = metrics_durable
        self.publish_async_max_pending = publish_async_max_pending
        self.registration_timeout_s = (
            registration_timeout_s
            if registration_timeout_s is not None
            else float(os.environ.get("REGISTRATION_TIMEOUT_S", "20"))
        )
        self.registration_max_retries = max(
            1,
            registration_max_retries
            if registration_max_retries is not None
            else int(os.environ.get("REGISTRATION_MAX_RETRIES", "5")),
        )
        self.registration_retry_backoff_s = (
            registration_retry_backoff_s
            if registration_retry_backoff_s is not None
            else float(os.environ.get("REGISTRATION_RETRY_BACKOFF_S", "1"))
        )
        self.metrics_pull_batch = metrics_pull_batch
        self.metrics_pull_timeout_s = metrics_pull_timeout_s
        self.metrics_wait_timeout_s = metrics_wait_timeout_s

        self.nc = None
        self.js = None
        self.metrics_by_id: dict[int, dict[str, Any]] = {}
        self.num_received = 0

        self._metrics_events: dict[int, asyncio.Event] = {}
        self._metrics_batch_waiters: list[tuple[set[int], asyncio.Event]] = []
        self._metrics_task: asyncio.Task[None] | None = None
        self._metrics_sub = None
        self._metrics_stop = asyncio.Event()
        self._no_responders_error = None
        self._nats_timeout_error = None

    async def __aenter__(self) -> "AsyncDeterSLClient":
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.close()

    async def connect(self, *, start_metrics: bool = False) -> "AsyncDeterSLClient":
        if self.nc is None:
            nats_module, no_responders_error, timeout_error = _load_nats()
            self._no_responders_error = no_responders_error
            self._nats_timeout_error = timeout_error
            self.nc = await nats_module.connect(self.url)
            self.js = self.nc.jetstream(publish_async_max_pending=self.publish_async_max_pending)
        if start_metrics:
            await self.start_metrics()
        return self

    async def close(self) -> None:
        await self.stop_metrics()
        if self.nc is None:
            return
        try:
            # The benchmark code waits for request/reply and JetStream publish acks
            # before shutdown, so a full connection drain is unnecessary here.
            # Using close avoids drain timeouts caused by lingering pull subscriptions.
            try:
                await self.nc.flush()
            except Exception:
                pass
            await self.nc.close()
        except Exception:
            pass
        finally:
            self.nc = None
            self.js = None

    async def start_metrics(self) -> None:
        self._require_connection()
        if self._metrics_task is not None:
            return
        self._metrics_stop = asyncio.Event()
        self._metrics_sub = await self.js.pull_subscribe(
            self.metrics_subject,
            durable=self.metrics_durable,
        )
        self._metrics_task = asyncio.create_task(self._metrics_pull_loop(self._metrics_sub))

    async def stop_metrics(self) -> None:
        if self._metrics_task is None:
            return
        self._metrics_stop.set()
        self._metrics_task.cancel()
        try:
            await self._metrics_task
        except asyncio.CancelledError:
            pass
        finally:
            self._metrics_task = None
        if self._metrics_sub is not None:
            try:
                await self._metrics_sub.unsubscribe()
            except Exception:
                pass
            finally:
                self._metrics_sub = None

    async def register_function(
        self,
        function_name: str,
        *,
        function_dir: str | Path,
    ) -> Any:
        path = Path(function_dir) / f"{function_name}.json"
        with path.open(encoding="utf-8") as file:
            payload = json.load(file)
        return await self._request_with_retries(
            f"{self.core_subject}.register_wasm",
            payload,
            f"function {function_name}",
        )

    async def register_functions(
        self,
        function_names: Iterable[str],
        *,
        function_dir: str | Path,
    ) -> None:
        for function_name in function_names:
            await self.register_function(function_name, function_dir=function_dir)

    async def register_workflow(self, payload: Mapping[str, Any]) -> Any:
        workflow_id = payload.get("id", "<unknown>")
        return await self._request_with_retries(
            f"{self.core_subject}.register_workflow",
            payload,
            f"workflow {workflow_id}",
        )

    async def register_workflows(self, workflows: Iterable[Mapping[str, Any]]) -> None:
        for workflow in workflows:
            await self.register_workflow(workflow)

    async def get_resource(self, resource_name: str, *, timeout_s: float = 10.0) -> bytes:
        self._require_connection()
        response = await self.nc.request(
            f"{self.core_subject}.get_resource",
            json.dumps({"res_name": resource_name}).encode("utf-8"),
            timeout=timeout_s,
        )
        return response.data

    async def invoke_workflow(
        self,
        workflow_id: str,
        workflow_input: Mapping[str, Any],
        *,
        can_abort: bool | None = None,
    ):
        payload: dict[str, Any] = {
            "workflow_id": workflow_id,
            "input": dict(workflow_input),
        }
        if can_abort is not None:
            payload["can_abort"] = can_abort
        return await self.publish_invocation(payload)

    async def publish_invocation(self, payload: Mapping[str, Any]):
        self._require_connection()
        return await self.js.publish_async(
            self.invoke_subject,
            json.dumps(payload).encode("utf-8"),
        )

    async def publish_invocations(self, payloads: Iterable[Mapping[str, Any]]) -> list[int]:
        futures = []
        for payload in payloads:
            futures.append(await self.publish_invocation(payload))

        request_ids: list[int] = []
        for fut in asyncio.as_completed(futures):
            ack = await fut
            request_ids.append(int(ack.seq))
        return request_ids

    async def wait_for_request(self, request_id: int, *, timeout: float = 30.0) -> dict[str, Any] | None:
        if request_id in self.metrics_by_id:
            return self.metrics_by_id[request_id]

        evt = self._metrics_events.setdefault(request_id, asyncio.Event())
        try:
            await asyncio.wait_for(evt.wait(), timeout=timeout)
        except asyncio.TimeoutError:
            return None
        return self.metrics_by_id.get(request_id)

    async def wait_for_requests(
        self,
        request_ids: Iterable[int] | Mapping[int, Any],
        *,
        timeout: float | None = None,
    ) -> dict[int, dict[str, Any]]:
        wait_timeout = self.metrics_wait_timeout_s if timeout is None else timeout
        if isinstance(request_ids, Mapping):
            request_metadata = dict(request_ids)
            request_id_list = [int(request_id) for request_id in request_metadata.keys()]
        else:
            request_metadata = None
            request_id_list = [int(request_id) for request_id in request_ids]

        if not request_id_list:
            return {}

        pending = {request_id for request_id in request_id_list if request_id not in self.metrics_by_id}
        if pending:
            evt = asyncio.Event()
            waiter = (pending, evt)
            self._metrics_batch_waiters.append(waiter)
            try:
                await asyncio.wait_for(evt.wait(), timeout=wait_timeout)
            except asyncio.TimeoutError as exc:
                missing = list(pending)
                sample = ", ".join(str(request_id) for request_id in missing[:10])
                sample_status = ", ".join(
                    f"{request_id}={'seen' if request_id in self.metrics_by_id else 'unseen'}"
                    for request_id in missing[:10]
                )
                extra = ""
                if request_metadata is not None and missing:
                    extra = f"; first missing request: {missing[0]} : {request_metadata.get(missing[0])}"
                raise RuntimeError(
                    f"Timed out after {wait_timeout}s waiting for "
                    f"{len(missing)}/{len(request_id_list)} workflow completions"
                    + (f"; first missing request_ids: {sample}" if sample else "")
                    + extra
                    + (f"; metrics_by_id status: {sample_status}" if sample_status else "")
                    + f"; total received metrics so far: {len(self.metrics_by_id)}; "
                    + f"total received messages: {self.num_received}"
                ) from exc
            finally:
                self._metrics_batch_waiters = [
                    current for current in self._metrics_batch_waiters if current is not waiter
                ]

        return {request_id: self.metrics_by_id[request_id] for request_id in request_id_list}

    async def _request_with_retries(
        self,
        subject: str,
        payload: Mapping[str, Any],
        label: str,
    ) -> Any:
        self._require_connection()
        nats_errors = (self._no_responders_error, self._nats_timeout_error)
        encoded = json.dumps(payload).encode("utf-8")

        for attempt in range(1, self.registration_max_retries + 1):
            try:
                response = await self.nc.request(subject, encoded, timeout=self.registration_timeout_s)
                raw_response = response.data.decode(errors="replace")
                try:
                    body = json.loads(raw_response)
                except json.JSONDecodeError:
                    body = None

                if isinstance(body, dict) and body.get("status") == "error":
                    error = str(body.get("error", raw_response))
                    if "already registered" in error:
                        print(f"{label} already registered: {error}")
                        return body
                    raise RuntimeError(f"{label} registration failed: {error}")

                print(f"{label} registered: {raw_response or '<empty response>'}")
                return body if body is not None else raw_response
            except nats_errors as exc:
                failure = "had no responders" if isinstance(exc, self._no_responders_error) else "timed out"
                if attempt == self.registration_max_retries:
                    raise RuntimeError(
                        f"{label} {failure} after {self.registration_max_retries} attempts "
                        f"with a {self.registration_timeout_s:g}s timeout"
                    ) from exc

                delay = self.registration_retry_backoff_s * attempt
                print(
                    f"{label} registration {failure} "
                    f"(attempt {attempt}/{self.registration_max_retries}); retrying in {delay:g}s"
                )
                await asyncio.sleep(delay)

        raise RuntimeError(f"{label} registration failed unexpectedly")

    async def _metrics_pull_loop(self, sub) -> None:
        nats_timeout_error = self._nats_timeout_error
        while not self._metrics_stop.is_set():
            try:
                msgs = await sub.fetch(self.metrics_pull_batch, timeout=self.metrics_pull_timeout_s)
            except nats_timeout_error:
                continue
            except asyncio.CancelledError:
                break

            for msg in msgs:
                try:
                    payload = json.loads(msg.data.decode())
                    request_id = _coerce_request_id(payload.get("request_id"))
                    if request_id is None:
                        continue

                    self.num_received += 1
                    self.metrics_by_id[request_id] = payload

                    evt = self._metrics_events.get(request_id)
                    if evt is not None:
                        evt.set()

                    for pending, batch_evt in self._metrics_batch_waiters:
                        pending.discard(request_id)
                        if not pending:
                            batch_evt.set()
                finally:
                    await msg.ack()

    def _require_connection(self) -> None:
        if self.nc is None or self.js is None:
            raise RuntimeError("DeterSL client is not connected")
