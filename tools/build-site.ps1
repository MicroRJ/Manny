param(
	[string]$Pandoc = "pandoc"
)

$repository = Split-Path -Parent $PSScriptRoot
$site = Join-Path $repository "site"
$docs = Join-Path $repository "docs"
$output = Join-Path (Join-Path $repository "build") "site"
$template = Join-Path $site "article.html"

$pages = Get-ChildItem -LiteralPath $docs -Filter "*.md" -File | Sort-Object Name

New-Item -ItemType Directory -Force -Path $output | Out-Null

foreach ($asset in @("index.html", "style.css", ".nojekyll"))
{
	Copy-Item -LiteralPath (Join-Path $site $asset) -Destination (Join-Path $output $asset) -Force
}

foreach ($page in $pages)
{
	$source = $page.FullName
	$output_name = [System.IO.Path]::ChangeExtension($page.Name, ".html")
	$destination = Join-Path $output $output_name
	& $Pandoc $source --from=gfm+yaml_metadata_block --to=html5 --standalone --syntax-highlighting=none --wrap=none --template=$template --output=$destination
	if ($LASTEXITCODE -ne 0)
	{
		throw "pandoc failed while generating $output_name"
	}
}

Write-Output "Generated $($pages.Count) documentation pages in $output"
